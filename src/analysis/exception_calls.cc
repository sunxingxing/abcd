#include "abcd/analysis/exception_calls.h"

#include <stdexcept>
#include "idf_scratch.h"

namespace abcd::analysis {
namespace {
bool SpillReload(const SemanticInstruction& spill, const SemanticInstruction& reload) {
  const auto* save = std::get_if<ScalarAssignment>(&spill.operation);
  const auto* fetch = std::get_if<ScalarAssignment>(&reload.operation);
  if (!save || !fetch || save->completion != CompletionEffect::kNormal ||
      fetch->completion != CompletionEffect::kNormal) return false;
  const auto* slot = std::get_if<abc::RegisterId>(&save->destination);
  const auto* saved = std::get_if<ScalarValue>(&save->expression);
  const auto* fetched = std::get_if<ScalarValue>(&fetch->expression);
  const auto* source = fetched ? std::get_if<abc::RegisterId>(fetched) : nullptr;
  return slot && saved && std::holds_alternative<Accumulator>(*saved) && source &&
         *source == *slot && std::holds_alternative<Accumulator>(fetch->destination);
}
}
std::variant<ExceptionCalls, ExceptionCallError> ProveExceptionCalls(
    const ExceptionFlow& flow, SemanticBudget budget) {
  using Error = ExceptionCallError;
  struct Sites { std::optional<ExceptionNodeId> before, commit; };
  enum class Entries : std::uint8_t { kNone, kSingle, kMultiple };
  // Canonical intervals accepted by the shared AST's callee copy-chain proof.
  constexpr std::size_t kDirectDistance = 1;
  constexpr std::size_t kSpillDistance = 3;
  try {
    ExceptionCalls result;
    const auto charge = [&] {
      if (result.work_units == budget.work) throw Error::kResourceLimit;
      ++result.work_units;
    };
    if (flow.nodes.empty() || flow.nodes.size() > UINT32_MAX ||
        flow.instructions.size() > UINT32_MAX) return Error::kInvalidFlow;
    detail::IdfScratch memory{budget.bytes};
    std::pmr::vector<Sites> sites(flow.instructions.size(), {}, &memory);
    std::pmr::vector<Entries> entries(flow.nodes.size(), Entries::kNone, &memory);
    for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
      charge();
      const auto& node = flow.nodes[i];
      if (node.instruction.value >= sites.size() || node.successor_count > node.successors.size())
        return Error::kInvalidFlow;
      auto& site = sites[node.instruction.value];
      const ExceptionNodeId id{static_cast<std::uint32_t>(i)};
      switch (node.phase) {
        case ExceptionPhase::kBefore:
          if (site.before) return Error::kInvalidFlow;
          site.before = id;
          break;
        case ExceptionPhase::kCommit:
          if (site.commit) return Error::kInvalidFlow;
          site.commit = id;
          break;
        case ExceptionPhase::kCatch: break;
      }
      for (std::size_t e = 0; e < node.successor_count; ++e) {
        charge();
        const auto target = node.successors[e].target.value;
        if (target >= entries.size()) return Error::kInvalidFlow;
        entries[target] = entries[target] == Entries::kNone ? Entries::kSingle : Entries::kMultiple;
      }
    }
    if (sites.size() > memory.Available() / sizeof(std::optional<InstructionId>)) return Error::kResourceLimit;
    result.properties.resize(sites.size());
    memory.Retain(result.AllocatedBytes());
    struct Exit { std::optional<ExceptionNodeId> handler; };
    const auto route = [&](const Sites& site) -> std::optional<Exit> {
      if (!site.before || !site.commit) return {};
      const auto& node = flow.nodes[site.before->value];
      if (!node.exception || !std::holds_alternative<PendingException>(node.exception->exception)) return {};
      bool normal = false;
      Exit exit;
      for (std::size_t e = 0; e < node.successor_count; ++e) {
        charge();
        const auto& arc = node.successors[e];
        switch (arc.kind) {
          case ExceptionArcKind::kNormal:
            if (normal || arc.target != *site.commit) return {};
            normal = true;
            break;
          case ExceptionArcKind::kException:
            if (exit.handler || !node.exception->target ||
                flow.nodes[arc.target.value].phase != ExceptionPhase::kCatch) return {};
            exit.handler = arc.target;
            break;
          case ExceptionArcKind::kTaken:
          case ExceptionArcKind::kFallthrough: return {};
        }
      }
      if (!normal || exit.handler.has_value() != node.exception->target.has_value()) return {};
      return exit;
    };
    for (std::size_t call = kDirectDistance; call < sites.size(); ++call) {
      charge();
      if (!std::holds_alternative<ReceiverCall>(flow.instructions[call].operation)) continue;
      std::optional<std::size_t> property;
      if (std::holds_alternative<NamedRead>(flow.instructions[call - kDirectDistance].operation))
        property = call - kDirectDistance;
      else if (call >= kSpillDistance &&
          std::holds_alternative<NamedRead>(flow.instructions[call - kSpillDistance].operation) &&
          SpillReload(flow.instructions[call - kSpillDistance + kDirectDistance],
                      flow.instructions[call - kDirectDistance]))
        property = call - kSpillDistance;
      if (!property) continue;
      const auto read_exit = route(sites[*property]);
      const auto call_exit = route(sites[call]);
      if (!read_exit || !call_exit || read_exit->handler != call_exit->handler ||
          flow.nodes[sites[*property].before->value].exception->target !=
              flow.nodes[sites[call].before->value].exception->target) continue;
      bool linear = true;
      for (auto i = *property; i < call; ++i) {
        charge();
        if (!sites[i].commit || !sites[i + 1].before) { linear = false; break; }
        const auto& commit = flow.nodes[sites[i].commit->value];
        if (commit.successor_count != 1 ||
            commit.successors.front().kind != ExceptionArcKind::kFallthrough ||
            commit.successors.front().target != *sites[i + 1].before ||
            entries[sites[i + 1].before->value] != Entries::kSingle) { linear = false; break; }
        if (i == *property) continue;
        const auto& before = flow.nodes[sites[i].before->value];
        const auto& effects = flow.instructions[i].effects;
        if (before.exception || before.successor_count != 1 ||
            before.successors.front().kind != ExceptionArcKind::kNormal ||
            before.successors.front().target != *sites[i].commit ||
            effects.normal != LanguageEffect::kNone || effects.before_throw != LanguageEffect::kNone ||
            effects.exceptional != ExceptionalCompletion::kImpossible) { linear = false; break; }
      }
      if (linear) result.properties[call] = InstructionId{static_cast<std::uint32_t>(*property)};
    }
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::analysis
