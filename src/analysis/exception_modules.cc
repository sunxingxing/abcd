#include "abcd/analysis/exception_modules.h"

#include <algorithm>
#include <bit>
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
    const auto charge = [&](std::uint64_t work = 1) {
      if (work > budget.work - result.work_units) throw Error::kResourceLimit;
      result.work_units += work;
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
    // (exception source node, value) for phi inputs carried by an exception
    // arc: such a value is live in a handler. A Hole copied before a guard
    // that throws to a local Catch must not be one of them.
    struct Carried { std::uint32_t node, value; };
    std::pmr::vector<Carried> carried{&memory};
    for (const auto& input : ssa.AllInputs()) {
      charge();
      const auto* edge = std::get_if<SsaEdge>(&input.edge);
      if (!edge || edge->source.value >= flow.nodes.size()) continue;
      const auto& source = flow.nodes[edge->source.value];
      if (edge->successor >= source.successor_count) return Error::kInvalidFlow;
      if (source.successors[edge->successor].kind == ExceptionArcKind::kException)
        carried.push_back({edge->source.value, input.value.value});
    }
    const auto by_site = [](const Carried& a, const Carried& b) {
      return a.node < b.node || (a.node == b.node && a.value < b.value);
    };
    std::sort(carried.begin(), carried.end(), by_site);
    charge(carried.size() * std::bit_width(carried.size()) + 1);
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
      // es2abc may copy the loaded value to registers before its guard
      // (`sta vN`); each copy is a non-throwing single-entry step.
      std::size_t at = i + 1;
      const auto copy = [&](std::size_t k) {
        const auto* assignment = std::get_if<ScalarAssignment>(&flow.instructions[k].operation);
        const auto* from = assignment ? std::get_if<ScalarValue>(&assignment->expression) : nullptr;
        return assignment && assignment->completion == CompletionEffect::kNormal &&
            std::holds_alternative<abc::RegisterId>(assignment->destination) && from &&
            std::holds_alternative<Accumulator>(*from);
      };
      while (at < sites.size() && copy(at)) { charge(); ++at; }
      if (at == sites.size()) continue;
      const auto* guard = std::get_if<HoleGuard>(&flow.instructions[at].operation);
      if (!guard || !std::holds_alternative<Accumulator>(guard->value) ||
          !std::holds_alternative<HoleNameSlot>(guard->name)) continue;
      const auto& load_effect = flow.instructions[i].effects;
      const auto& guard_effect = flow.instructions[at].effects;
      if (!load_effect.normal_registers.write ||
          !std::holds_alternative<Accumulator>(*load_effect.normal_registers.write) ||
          guard_effect.normal_registers.write ||
          load_effect.normal != ModuleReadEffects(std::get<ModuleLoad>(flow.instructions[i].operation).loading) ||
          load_effect.before_throw != load_effect.normal ||
          guard_effect.normal != LanguageEffect::kGuard ||
          guard_effect.before_throw != LanguageEffect::kGuard) continue;
      if (!sites[i].before || !sites[i].commit || !sites[at].before || !sites[at].commit)
        return Error::kInvalidFlow;
      const auto before = *sites[i].before;
      const auto commit = *sites[i].commit;
      const auto next = *sites[at].before;
      // Every step from the load Commit to the guard Before is a single
      // fallthrough into a single-entry node; copies take their normal arc.
      const auto falls_to = [&](ExceptionNodeId from, ExceptionNodeId to) {
        const auto& node = flow.nodes[from.value];
        return node.successor_count == 1 && node.successors.front().kind == ExceptionArcKind::kFallthrough &&
            node.successors.front().target == to && incoming[to.value] == Entries::kSingle;
      };
      bool chained = true;
      auto last = commit;
      for (auto k = i + 1; chained && k < at; ++k) {
        charge();
        if (!sites[k].before || !sites[k].commit) return Error::kInvalidFlow;
        const auto& step = flow.nodes[sites[k].before->value];
        chained = falls_to(last, *sites[k].before) && !step.exception && step.successor_count == 1 &&
            step.successors.front().kind == ExceptionArcKind::kNormal &&
            step.successors.front().target == *sites[k].commit && sites[k].written;
        last = *sites[k].commit;
      }
      const auto load_exit = route(before, commit);
      const auto guard_exit = route(next, *sites[at].commit);
      if (!chained || !falls_to(last, next) || !load_exit || !guard_exit ||
          load_exit->handler != guard_exit->handler ||
          flow.nodes[before.value].exception->target != flow.nodes[next.value].exception->target ||
          !sites[i].written) continue;
      // A local Catch would observe each copied Hole unless its value is dead
      // on the guard's exception arc (pruned SSA: no phi input there).
      bool hidden = true;
      for (auto k = i + 1; hidden && guard_exit->handler && k < at; ++k) {
        charge(std::bit_width(carried.size()) + 1);
        const auto value = ssa.Write(*sites[k].commit);
        if (!value) return Error::kInvalidFlow;
        hidden = !std::binary_search(carried.begin(), carried.end(), Carried{next.value, value->value}, by_site);
      }
      if (!hidden) continue;
      // Besides dead copies, the only intervening write is load's accumulator.
      // A shared Catch overwrites it before handler code.
      result.reads.push_back({InstructionId{static_cast<std::uint32_t>(i)},
          load_exit->handler ? ModuleReadProof::kCatchGuarded : ModuleReadProof::kCallerGuarded,
          std::nullopt, InstructionId{static_cast<std::uint32_t>(at)}});
      result.loads.push_back({commit, load_exit->handler
          ? ExceptionLoadProof::kCatchGuardedModule : ExceptionLoadProof::kCallerGuardedModule});
    }
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::analysis
