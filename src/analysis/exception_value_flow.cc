#include "abcd/analysis/exception_value_flow.h"

#include <algorithm>
#include <stdexcept>
#include "value_propagation.h"
#include "abcd/analysis/exception_throw_domain.h"

namespace abcd::analysis {
std::variant<ExceptionValueFlow, ValueFlowFailure> ExceptionValueFlow::Build(
    const ExceptionFlow& flow, const ExceptionSsa& ssa,
    std::span<const EntryValueFact> entries, IdfQuota& quota, IdfBudget budget,
    PendingExceptionValues pending_values,
    std::span<const ExceptionLoadFact> loads,
    std::span<const ExceptionPendingFact> pending_facts,
    CallResultValues calls, FrameReceiver receiver, ArithmeticSemantics arithmetic, ExceptionThrowRefinement throws, GlobalReadValues globals) {
  using Error = ValueFlowError;
  using Kind = detail::ValueRuleKind;
  const auto invalid = [] { return ValueFlowFailure{Error::kInvalidSsa, {}, {}}; };
  const auto same_slot = [](ScalarSlot left, ScalarSlot right) {
    const auto* a = std::get_if<abc::RegisterId>(&left);
    const auto* b = std::get_if<abc::RegisterId>(&right);
    return a && b ? *a == *b : !a && !b;
  };
  try {
    const auto consumed = quota.Consumed();
    const auto charge = [&](std::uint64_t units = 1) {
      if (!quota.Take(units)) throw detail::IdfWorkLimit{};
    };
    const auto count = ssa.Values().size();
    if (count > UINT32_MAX || flow.nodes.empty() || flow.nodes.size() > UINT32_MAX)
      return invalid();
    detail::IdfScratch memory{budget.bytes};
    charge(count + flow.nodes.size() + flow.instructions.size());
    std::pmr::vector<detail::ValueNode> nodes(count, {}, &memory);
    std::pmr::vector<ValueKinds> facts(count, {}, &memory);
    std::pmr::vector<std::uint8_t> marked(count, 0, &memory);
    std::pmr::vector<std::uint8_t> trusted{&memory};
    if (!loads.empty()) trusted.resize(count);
    for (const auto& load : loads) {
      charge();
      const InstructionId site{load.commit.value};
      if (load.commit.value >= flow.nodes.size())
        return ValueFlowFailure{Error::kInvalidLoadFact, site, {}};
      const auto& node = flow.nodes[load.commit.value];
      if (node.phase != ExceptionPhase::kCommit ||
          node.instruction.value >= flow.instructions.size())
        return ValueFlowFailure{Error::kInvalidLoadFact, site, {}};
      const auto& operation = flow.instructions[node.instruction.value].operation;
      switch (load.proof) {
        case ExceptionLoadProof::kLanguage: break;
        case ExceptionLoadProof::kCallerGuardedModule:
        case ExceptionLoadProof::kCatchGuardedModule:
          if (!std::holds_alternative<ModuleLoad>(operation))
            return ValueFlowFailure{Error::kInvalidLoadFact, site, {}};
          break;
      }
      const auto value = ssa.Write(load.commit);
      if ((!std::holds_alternative<LexicalLoad>(operation) &&
           !std::holds_alternative<ModuleLoad>(operation)) ||
          !value || value->value >= count)
        return ValueFlowFailure{Error::kInvalidLoadFact, site, {}};
      const auto* origin = std::get_if<InstructionValue>(&ssa.Values()[value->value].origin);
      if (!origin || origin->instruction != site)
        return ValueFlowFailure{Error::kInvalidLoadFact, site, {}};
      if (trusted[value->value])
        return ValueFlowFailure{Error::kDuplicateLoadFact, site, {}};
      trusted[value->value] = 1;
    }
    std::pmr::vector<detail::ValueDependency> edges{&memory};
    std::pmr::vector<std::optional<ExceptionNodeId>> before(flow.instructions.size(), {}, &memory);
    std::pmr::vector<std::uint8_t> reached(flow.nodes.size(), 0, &memory);
    std::pmr::vector<ExceptionNodeId> pending{&memory};
    for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
      charge();
      const auto& node = flow.nodes[i];
      if (node.instruction.value >= before.size() || node.successor_count > node.successors.size())
        return invalid();
      if (node.phase == ExceptionPhase::kBefore) {
        if (before[node.instruction.value]) return invalid();
        before[node.instruction.value] = ExceptionNodeId{static_cast<std::uint32_t>(i)};
      }
    }
    pending.push_back({0});
    reached[0] = 1;
    for (std::size_t head = 0; head < pending.size(); ++head) {
      charge();
      const auto& node = flow.nodes[pending[head].value];
      for (std::size_t e = 0; e < node.successor_count; ++e) {
        charge();
        const auto target = node.successors[e].target;
        if (target.value >= reached.size()) return invalid();
        if (!reached[target.value]) {
          reached[target.value] = 1;
          pending.push_back(target);
        }
      }
    }
    std::pmr::vector<std::optional<ValueKinds>> pending_kinds{&memory};
    if (!pending_facts.empty()) pending_kinds.resize(flow.nodes.size());
    for (const auto& fact : pending_facts) {
      charge();
      const InstructionId site{fact.before.value};
      if (fact.before.value >= flow.nodes.size() || fact.kinds.IsEmpty())
        return ValueFlowFailure{Error::kInvalidPendingFact, site, {}};
      const auto& node = flow.nodes[fact.before.value];
      if (!reached[fact.before.value] || node.phase != ExceptionPhase::kBefore ||
          !node.exception || !std::holds_alternative<PendingException>(node.exception->exception))
        return ValueFlowFailure{Error::kInvalidPendingFact, site, {}};
      if (pending_kinds[fact.before.value])
        return ValueFlowFailure{Error::kDuplicatePendingFact, site, {}};
      pending_kinds[fact.before.value] = fact.kinds;
    }
    RawEventArena throw_arena{{memory.Available(), quota.Remaining()}};
    std::pmr::vector<ExceptionThrowDomain> throw_domains{throw_arena.Resource()};
    if (throws == ExceptionThrowRefinement::kGuardedNonHole) {
      throw_domains = DeriveExceptionThrowDomains(flow, ssa, throw_arena);
      VerifyExceptionThrowDomains(flow, ssa, throw_domains, throw_arena);
      charge(throw_arena.Work()); memory.Observe(throw_arena.Peak());
      memory.Retain(throw_domains.capacity() * sizeof(ExceptionThrowDomain));
    }
    const auto connect = [&](SsaValueId from, SsaValueId to, bool exclude_hole = false) {
      charge();
      if (from.value >= count || to.value >= count) throw invalid();
      edges.push_back({{from.value}, {to.value}, exclude_hole});
    };
    for (std::size_t i = 0; i < count; ++i) {
      charge();
      const SsaValueId target{static_cast<std::uint32_t>(i)};
      const auto& value = ssa.Values()[i];
      if (std::holds_alternative<MethodEntry>(value.origin)) {
        facts[i] = ValueKinds::Unknown();
      } else if (std::holds_alternative<PhiValue>(value.origin)) {
        nodes[i].kind = Kind::kJoin;
        for (const auto& input : ssa.Inputs(target)) {
          if (input.value.value >= count ||
              !same_slot(value.slot, ssa.Values()[input.value.value].slot)) return invalid();
          connect(input.value, target);
        }
      } else {
        const auto site = std::get<InstructionValue>(value.origin).instruction.value;
        if (site >= flow.nodes.size() || ssa.Write({site}) != target) return invalid();
        const auto& node = flow.nodes[site];
        if (node.phase == ExceptionPhase::kCatch) {
          if (!std::holds_alternative<Accumulator>(value.slot)) return invalid();
          nodes[i].kind = Kind::kJoin;
          continue;
        }
        if (node.phase != ExceptionPhase::kCommit || !before[node.instruction.value])
          return invalid();
        const auto& destination = flow.instructions[node.instruction.value].effects.normal_registers.write;
        if (!destination || !same_slot(value.slot, *destination)) return invalid();
        // Owner premises affect only normal Commit values. They never move a
        // load's exception edge or upgrade the handler's pending exception.
        // Guarded facts are conditional lowered-pair values: their original
        // Hole state is discarded on caller exit or overwritten at Catch.
        if (!trusted.empty() && trusted[i]) {
          facts[i] = ValueKinds::Language();
          continue;
        }
        const auto described = detail::DescribeRegisterValue(
            flow.instructions[node.instruction.value], calls, receiver, arithmetic, globals);
        if (const auto* error = std::get_if<ScalarError>(&described))
          return ValueFlowFailure{Error::kInstruction, InstructionId{site}, *error};
        const auto rule = std::get<detail::ValueRule>(described);
        nodes[i].kind = rule.kind;
        nodes[i].arithmetic = rule.arithmetic;
        if (rule.kind == Kind::kFixed) { facts[i] = rule.seed; continue; }
        // Refined normal results depend on checked input facts; seeding a
        // Boolean/String result early could certify an unproved input cycle.
        nodes[i].normal_result = rule.seed.IsEmpty() ? ValueKinds::Language() : rule.seed;
        // Commit has no operand reads. Its value depends on the original
        // operation's Before reads, not the handler's overwritten accumulator.
        auto reads = ssa.Reads(*before[node.instruction.value]);
        if (rule.input) {
          if (*rule.input >= reads.size()) return invalid();
          reads = reads.subspan(*rule.input, 1);
        }
        if (reads.empty() || reads.size() > detail::kScalarInputs ||
            (rule.kind == Kind::kJoin && reads.size() != 1)) return invalid();
        for (const auto& read : reads) {
          connect(read.value, target);
          nodes[i].inputs[nodes[i].input_count++] = read.value;
        }
      }
    }
    std::size_t throw_cursor = 0;
    for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
      charge();
      while (throw_cursor < throw_domains.size() && throw_domains[throw_cursor].site.value < i) ++throw_cursor;
      const auto& node = flow.nodes[i];
      if (!reached[i] || !node.exception) continue;
      for (std::size_t e = 0; e < node.successor_count; ++e) {
        charge();
        const auto& edge = node.successors[e];
        if (edge.kind != ExceptionArcKind::kException) continue;
        if (flow.nodes[edge.target.value].phase != ExceptionPhase::kCatch) return invalid();
        const auto target = ssa.Write(edge.target);
        if (!target || target->value >= count) return invalid();
        if (const auto* thrown = std::get_if<ThrownValue>(&node.exception->exception)) {
          const auto reads = ssa.Reads({static_cast<std::uint32_t>(i)});
          if (!std::holds_alternative<Accumulator>(thrown->value) || reads.size() != 1)
            return invalid();
          const bool filtered = throw_cursor < throw_domains.size() && throw_domains[throw_cursor].site.value == i;
          connect(reads.front().value, *target, filtered);
        } else {
          // User callbacks can supply the pending value. Without an owner
          // premise, do not infer Language merely because the VM threw it.
          const auto owner = pending_kinds.empty() ? std::optional<ValueKinds>{} : pending_kinds[i];
          const auto kinds = owner.value_or(pending_values == PendingExceptionValues::kLanguage
              ? ValueKinds::Language() : ValueKinds::Unknown());
          facts[target->value] = facts[target->value].Join(kinds);
        }
      }
    }
    for (const auto& entry : entries) {
      charge();
      const auto id = entry.value.value;
      if (id >= count || entry.kinds.IsEmpty() ||
          !std::holds_alternative<MethodEntry>(ssa.Values()[id].origin))
        return ValueFlowFailure{Error::kInvalidEntryFact, {}, {}};
      if (marked[id]) return ValueFlowFailure{Error::kDuplicateEntryFact, {}, {}};
      marked[id] = 1;
      facts[id] = entry.kinds;
    }
    ExceptionValueFlow result;
    detail::PropagateValues(nodes, edges, facts, marked, memory, charge, result.work);
    charge(count);
    if (count > memory.Available() / sizeof(ValueKinds)) throw std::bad_alloc{};
    if (count) {
      result.facts_ = std::make_unique<ValueKinds[]>(count);
      std::copy(facts.begin(), facts.end(), result.facts_.get());
    }
    result.count_ = count;
    memory.Observe(result.AllocatedBytes());
    result.work.peak_bytes = memory.Peak();
    result.work.units = quota.Consumed() - consumed;
    return result;
  } catch (const ValueFlowFailure& failure) { return failure;
  } catch (const RawEventFailure& failure) {
    return ValueFlowFailure{failure.kind == RawEventError::kWorkLimit ? ValueFlowError::kWorkLimit :
        failure.kind == RawEventError::kResourceLimit ? ValueFlowError::kResourceLimit : ValueFlowError::kInvalidSsa, {}, {}};
  } catch (const detail::IdfWorkLimit&) { return ValueFlowFailure{Error::kWorkLimit, {}, {}};
  } catch (const std::bad_alloc&) { return ValueFlowFailure{Error::kResourceLimit, {}, {}};
  } catch (const std::length_error&) { return ValueFlowFailure{Error::kResourceLimit, {}, {}}; }
}
}  // namespace abcd::analysis
