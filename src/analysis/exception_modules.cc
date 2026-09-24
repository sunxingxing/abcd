#include "abcd/analysis/exception_modules.h"

#include <stdexcept>
#include "idf_scratch.h"

namespace abcd::analysis {
std::variant<ExceptionModules, ExceptionModuleError> ProveExceptionModules(
    const ExceptionFlow& flow, const ExceptionSsa& ssa, SemanticBudget budget) {
  using Error = ExceptionModuleError;
  struct Sites {
    std::optional<ExceptionNodeId> before, commit;
    bool written = false;
  };
  enum class Entries : std::uint8_t { kNone, kSingle, kMultiple };
  try {
    ExceptionModules result;
    const auto charge = [&] {
      if (result.work_units == budget.work) throw Error::kResourceLimit;
      ++result.work_units;
    };
    if (flow.nodes.empty() || flow.nodes.size() > UINT32_MAX ||
        flow.instructions.size() > UINT32_MAX) return Error::kInvalidFlow;
    detail::IdfScratch memory{budget.bytes};
    std::pmr::vector<Sites> sites(flow.instructions.size(), {}, &memory);
    std::pmr::vector<Entries> incoming(flow.nodes.size(), Entries::kNone, &memory);
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
        if (target >= incoming.size()) return Error::kInvalidFlow;
        incoming[target] = incoming[target] == Entries::kNone ? Entries::kSingle : Entries::kMultiple;
      }
    }
    // Index reachable SSA definitions once instead of binary-searching the
    // write table for every candidate module pair.
    for (const auto& value : ssa.Values()) {
      charge();
      const auto* origin = std::get_if<InstructionValue>(&value.origin);
      if (!origin) continue;
      if (origin->instruction.value >= flow.nodes.size()) return Error::kInvalidFlow;
      const auto& node = flow.nodes[origin->instruction.value];
      if (node.phase == ExceptionPhase::kCommit) sites[node.instruction.value].written = true;
    }
    const auto reserve = [&]<class T>(std::vector<T>& items) {
      if (sites.size() > memory.Available() / sizeof(T)) throw Error::kResourceLimit;
      items.reserve(sites.size());
      memory.Retain(items.capacity() * sizeof(T));
    };
    reserve(result.reads);
    reserve(result.loads);
    const auto caller_only = [&](ExceptionNodeId id) {
      const auto& node = flow.nodes[id.value];
      return node.exception && !node.exception->target && node.successor_count == 1 &&
             node.successors.front().kind == ExceptionArcKind::kNormal;
    };
    for (std::size_t i = 0; i + 1 < sites.size(); ++i) {
      charge();
      if (!std::holds_alternative<ModuleLoad>(flow.instructions[i].operation)) continue;
      const auto* guard = std::get_if<HoleGuard>(&flow.instructions[i + 1].operation);
      if (!guard || !std::holds_alternative<Accumulator>(guard->value) ||
          !std::holds_alternative<HoleNameSlot>(guard->name)) continue;
      if (!sites[i].before || !sites[i].commit || !sites[i + 1].before || !sites[i + 1].commit)
        return Error::kInvalidFlow;
      const auto before = *sites[i].before;
      const auto commit = *sites[i].commit;
      const auto next = *sites[i + 1].before;
      const auto& completed = flow.nodes[commit.value];
      if (!caller_only(before) || !caller_only(next) ||
          flow.nodes[before.value].successors.front().target != commit ||
          flow.nodes[next.value].successors.front().target != *sites[i + 1].commit ||
          completed.successor_count != 1 ||
          completed.successors.front().kind != ExceptionArcKind::kFallthrough ||
          completed.successors.front().target != next ||
          incoming[next.value] != Entries::kSingle || !sites[i].written) continue;
      result.reads.push_back({InstructionId{static_cast<std::uint32_t>(i)}, ModuleReadProof::kCallerGuarded});
      result.loads.push_back({commit, ExceptionLoadProof::kCallerGuardedModule});
    }
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::analysis
