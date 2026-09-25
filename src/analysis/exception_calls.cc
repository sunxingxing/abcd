#include "abcd/analysis/exception_calls.h"

#include <stdexcept>
#include "idf_scratch.h"

namespace abcd::analysis {
namespace {
bool PrivateArgument(const SemanticOperation& operation) {
  if (std::holds_alternative<StringLoad>(operation)) return true;
  const auto* assignment = std::get_if<ScalarAssignment>(&operation);
  if (!assignment || assignment->completion != CompletionEffect::kNormal) return false;
  const auto* value = std::get_if<ScalarValue>(&assignment->expression);
  // Match the shared AST's private-value boundary. Allocation and coercion
  // cannot move across the delayed property read, even without a local catch.
  return value && !std::holds_alternative<EmptyArray>(*value);
}
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
    const ExceptionFlow& flow, const InstructionSsa& ssa, SemanticBudget budget) {
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
    if (!ssa.IsPhased() || flow.nodes.empty() || flow.nodes.size() > UINT32_MAX ||
        flow.instructions.size() > UINT32_MAX) return Error::kInvalidFlow;
    detail::IdfScratch memory{budget.bytes};
    std::pmr::vector<Sites> sites(flow.instructions.size(), {}, &memory);
    std::pmr::vector<Entries> entries(flow.nodes.size(), Entries::kNone, &memory);
    bool has_catch = false;
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
        case ExceptionPhase::kCatch: has_catch = true; break;
      }
      for (std::size_t e = 0; e < node.successor_count; ++e) {
        charge();
        const auto target = node.successors[e].target.value;
        if (target >= entries.size()) return Error::kInvalidFlow;
        entries[target] = entries[target] == Entries::kNone ? Entries::kSingle : Entries::kMultiple;
      }
    }
    if (sites.size() > memory.Available() / sizeof(std::optional<ExceptionCallRoute>)) return Error::kResourceLimit;
    result.routes.resize(sites.size());
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
    struct Barriers {
      std::size_t edge = 0, argument = 0, ordered = 0, dispatch = 0;
    };
    std::pmr::vector<Barriers> barriers(sites.size() + 1, {}, &memory);
    std::optional<DispatchTarget> previous_target;
    std::optional<ExceptionNodeId> previous_handler;
    bool seen_dispatch = false;
    // Prefix counts make arbitrarily long, overlapping candidate intervals
    // constant-time queries. All storage shares the stage's admission budget.
    for (std::size_t i = 0; i < sites.size(); ++i) {
      charge();
      bool linear = false;
      if (i + 1 < sites.size() && sites[i].commit && sites[i + 1].before) {
        const auto& commit = flow.nodes[sites[i].commit->value];
        linear = commit.successor_count == 1 &&
            commit.successors.front().kind == ExceptionArcKind::kFallthrough &&
            commit.successors.front().target == *sites[i + 1].before &&
            entries[sites[i + 1].before->value] == Entries::kSingle;
      }
      bool pure = false;
      bool ordered = false;
      bool dispatch_change = false;
      if (sites[i].before && sites[i].commit) {
        const auto& before = flow.nodes[sites[i].before->value];
        const auto& effects = flow.instructions[i].effects;
        const bool normal = !before.exception && before.successor_count == 1 &&
            before.successors.front().kind == ExceptionArcKind::kNormal &&
            before.successors.front().target == *sites[i].commit &&
            effects.exceptional == ExceptionalCompletion::kImpossible;
        const auto exit = before.exception ? route(sites[i]) : std::nullopt;
        ordered = normal || exit.has_value();
        if (exit) {
          dispatch_change = !seen_dispatch || previous_target != before.exception->target ||
                            previous_handler != exit->handler;
          previous_target = before.exception->target;
          previous_handler = exit->handler;
          seen_dispatch = true;
        }
        pure = normal &&
            effects.normal == LanguageEffect::kNone &&
            effects.before_throw == LanguageEffect::kNone &&
            PrivateArgument(flow.instructions[i].operation);
      }
      barriers[i + 1] = {barriers[i].edge + !linear,
                         barriers[i].argument + !pure,
                         barriers[i].ordered + !ordered,
                         barriers[i].dispatch + dispatch_change};
    }
    const auto callee_property = [&](std::size_t call) -> std::optional<std::size_t> {
      std::optional<SsaValueId> callee;
      for (const auto& read : ssa.Reads(InstructionId{static_cast<std::uint32_t>(call)})) {
        charge();
        const auto slot = ssa.Slot(read.value);
        if (slot && std::holds_alternative<Accumulator>(*slot)) callee = read.value;
      }
      // Match the shared AST's bounded saved-callee shape; do not walk an
      // arbitrary copy/phi graph per call or mistake a later argument getter
      // for the callee's original property read.
      constexpr std::size_t kCalleeCopies = 2;
      for (std::size_t copies = 0; callee && copies <= kCalleeCopies; ++copies) {
        charge();
        const auto origin = ssa.Definition(*callee);
        if (!origin || origin->value >= call) return {};
        const auto& operation = flow.instructions[origin->value].operation;
        if (std::holds_alternative<NamedRead>(operation)) return origin->value;
        const auto* assignment = std::get_if<ScalarAssignment>(&operation);
        if (!assignment || assignment->completion != CompletionEffect::kNormal) return {};
        const auto* value = std::get_if<ScalarValue>(&assignment->expression);
        if (!value || (!std::holds_alternative<Accumulator>(*value) &&
                       !std::holds_alternative<abc::RegisterId>(*value))) return {};
        const auto reads = ssa.Reads(*origin);
        if (reads.size() != 1) return {};
        callee = reads.front().value;
      }
      return {};
    };
    for (std::size_t call = 0; call < sites.size(); ++call) {
      charge();
      if (!std::holds_alternative<ReceiverCall>(flow.instructions[call].operation)) continue;
      const auto property = callee_property(call);
      if (!property) continue;
      const bool short_interval = call - *property == kDirectDistance ||
          (call - *property == kSpillDistance &&
          SpillReload(flow.instructions[call - kSpillDistance + kDirectDistance],
                      flow.instructions[call - kDirectDistance]));
      const auto read_exit = route(sites[*property]);
      const auto call_exit = route(sites[call]);
      if (!read_exit || !call_exit || read_exit->handler != call_exit->handler ||
          flow.nodes[sites[*property].before->value].exception->target !=
              flow.nodes[sites[call].before->value].exception->target) continue;
      if (!short_interval) {
        // Keep every exceptional evaluation inside the same original dispatch
        // and Catch gateway. A local catch forbids even pure-argument fusion:
        // getter failure must precede argument register writes. The staged
        // Reference call preserves those writes; AST SSA-use checks separately
        // reject any removed callee spill observed by a handler or its phis.
        const bool linear = barriers[call].edge == barriers[*property].edge;
        const bool same_dispatch = barriers[call + 1].dispatch == barriers[*property + 1].dispatch;
        if ((linear || !has_catch) && same_dispatch &&
            barriers[call].ordered == barriers[*property + 1].ordered) {
          const auto evaluation =
              !read_exit->handler && linear &&
                  barriers[call].argument == barriers[*property + 1].argument
                  ? ReceiverEvaluation::kFuse : ReceiverEvaluation::kPreserveOrder;
          result.routes[call] = ExceptionCallRoute{
              InstructionId{static_cast<std::uint32_t>(*property)}, evaluation,
              linear ? ReceiverRegion::kLinear : ReceiverRegion::kForward};
        }
        continue;
      }
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
      if (linear) result.routes[call] = ExceptionCallRoute{
          InstructionId{static_cast<std::uint32_t>(*property)},
          ReceiverEvaluation::kFuse, ReceiverRegion::kLinear};
    }
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::analysis
