#include "abcd/analysis/exception_modules.h"

#include <stdexcept>
#include "idf_scratch.h"

namespace abcd::analysis {
std::variant<ExceptionModules, ExceptionModuleError> ProveExceptionModules(
    const ExceptionFlow& flow, const ExceptionSsa& ssa, SemanticBudget budget,
    std::span<const LanguageLoadFact> initialized_modules) {
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
    std::optional<InstructionId> previous;
    for (const auto& premise : initialized_modules) {
      charge();
      const auto id = premise.instruction;
      if (id.value >= flow.instructions.size() || (previous && id <= *previous))
        return Error::kInvalidPremise;
      const auto* load = std::get_if<ModuleLoad>(&flow.instructions[id.value].operation);
      if (!load || load->space != ModuleSpace::kLocal) return Error::kInvalidPremise;
      previous = id;
    }
    detail::IdfScratch memory{budget.bytes};
    std::pmr::vector<Sites> sites(flow.instructions.size(), {}, &memory);
    std::pmr::vector<Entries> incoming(flow.nodes.size(), Entries::kNone, &memory);
    std::pmr::vector<std::uint8_t> catch_acc(flow.nodes.size(), 0, &memory);
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
      if (node.phase == ExceptionPhase::kCatch && std::holds_alternative<Accumulator>(value.slot))
        catch_acc[origin->instruction.value] = 1;
    }
    const auto reserve = [&]<class T>(std::vector<T>& items) {
      if (sites.size() > memory.Available() / sizeof(T)) throw Error::kResourceLimit;
      items.reserve(sites.size());
      memory.Retain(items.capacity() * sizeof(T));
    };
    reserve(result.reads);
    reserve(result.loads);
    struct Exit { std::optional<ExceptionNodeId> handler; };
    const auto route = [&](ExceptionNodeId id, ExceptionNodeId commit) -> std::optional<Exit> {
      const auto& node = flow.nodes[id.value];
      if (!node.exception || !std::holds_alternative<PendingException>(node.exception->exception)) return {};
      bool normal = false;
      Exit exit;
      for (std::size_t e = 0; e < node.successor_count; ++e) {
        charge();
        const auto& arc = node.successors[e];
        switch (arc.kind) {
          case ExceptionArcKind::kNormal:
            if (normal || arc.target != commit) return {};
            normal = true;
            break;
          case ExceptionArcKind::kException:
            if (exit.handler || !node.exception->target ||
                flow.nodes[arc.target.value].phase != ExceptionPhase::kCatch ||
                !catch_acc[arc.target.value]) return {};
            exit.handler = arc.target;
            break;
          case ExceptionArcKind::kTaken:
          case ExceptionArcKind::kFallthrough: return {};
        }
      }
      if (!normal || exit.handler.has_value() != node.exception->target.has_value()) return {};
      return exit;
    };
    std::size_t initialized = 0;
    for (std::size_t i = 0; i < sites.size(); ++i) {
      charge();
      if (!std::holds_alternative<ModuleLoad>(flow.instructions[i].operation)) continue;
      if (initialized < initialized_modules.size() && initialized_modules[initialized].instruction.value == i) {
        ++initialized;
        if (!sites[i].before || !sites[i].commit) return Error::kInvalidFlow;
        if (!sites[i].written) continue;
        result.reads.push_back({InstructionId{static_cast<std::uint32_t>(i)}, ModuleReadProof::kInitialized});
        result.loads.push_back({*sites[i].commit, ExceptionLoadProof::kLanguage});
        continue;
      }
      if (i + 1 == sites.size()) continue;
      const auto* guard = std::get_if<HoleGuard>(&flow.instructions[i + 1].operation);
      if (!guard || !std::holds_alternative<Accumulator>(guard->value) ||
          !std::holds_alternative<HoleNameSlot>(guard->name)) continue;
      const auto& load_effect = flow.instructions[i].effects;
      const auto& guard_effect = flow.instructions[i + 1].effects;
      if (!load_effect.normal_registers.write ||
          !std::holds_alternative<Accumulator>(*load_effect.normal_registers.write) ||
          guard_effect.normal_registers.write ||
          load_effect.normal != ModuleReadEffects(std::get<ModuleLoad>(flow.instructions[i].operation).loading) ||
          load_effect.before_throw != load_effect.normal ||
          guard_effect.normal != LanguageEffect::kGuard ||
          guard_effect.before_throw != LanguageEffect::kGuard) continue;
      if (!sites[i].before || !sites[i].commit || !sites[i + 1].before || !sites[i + 1].commit)
        return Error::kInvalidFlow;
      const auto before = *sites[i].before;
      const auto commit = *sites[i].commit;
      const auto next = *sites[i + 1].before;
      const auto& completed = flow.nodes[commit.value];
      const auto load_exit = route(before, commit);
      const auto guard_exit = route(next, *sites[i + 1].commit);
      if (!load_exit || !guard_exit || load_exit->handler != guard_exit->handler ||
          flow.nodes[before.value].exception->target != flow.nodes[next.value].exception->target ||
          completed.successor_count != 1 ||
          completed.successors.front().kind != ExceptionArcKind::kFallthrough ||
          completed.successors.front().target != next ||
          incoming[next.value] != Entries::kSingle || !sites[i].written) continue;
      // The only intervening write is load's accumulator. A shared Catch
      // overwrites it before handler code; no general register is relocated.
      result.reads.push_back({InstructionId{static_cast<std::uint32_t>(i)},
          load_exit->handler ? ModuleReadProof::kCatchGuarded : ModuleReadProof::kCallerGuarded});
      result.loads.push_back({commit, load_exit->handler
          ? ExceptionLoadProof::kCatchGuardedModule : ExceptionLoadProof::kCallerGuardedModule});
    }
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::analysis
