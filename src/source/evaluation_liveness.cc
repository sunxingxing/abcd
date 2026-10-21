#include "evaluation_liveness.h"
#include "for_of_evaluation.h"

#include <new>
#include <stdexcept>
#include "../analysis/value_edits.h"

namespace abcd::source {
bool HasRetainedEvaluationValue(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, analysis::ValueId id) {
  return RetainedEvaluationLifetime(graph, plan, id).has_value();
}
std::optional<EvaluationStorageLifetime> RetainedEvaluationLifetime(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, analysis::ValueId id) {
  using namespace analysis;
  if (id.value >= graph.values.size()) return {};
  const auto& value = graph.values[id.value];
  if (value.life != ValueLife::kLive || !value.domain.IsLanguage()) return {};
  if (plan.for_of && id.value < plan.for_of->bindings.size() && plan.for_of->bindings[id.value])
    return EvaluationStorageLifetime::kIteration;
  return std::visit([&](const auto& origin) -> std::optional<EvaluationStorageLifetime> {
    using T = std::decay_t<decltype(origin)>;
    if constexpr (std::is_same_v<T, EntryDefinition>) {
      if (std::holds_alternative<ParameterEntry>(origin) || std::holds_alternative<ReceiverEntry>(origin))
        return EvaluationStorageLifetime::kInvocation;
    } else if constexpr (std::is_same_v<T, PhiDefinition>) {
      if (plan.phis) return EvaluationStorageLifetime::kInvocation;
    } else if constexpr (std::is_same_v<T, CatchDefinition>) {
      if (plan.exceptions) return EvaluationStorageLifetime::kHandler;
    } else {
      static_assert(std::is_same_v<T, OpResult>);
      if (origin.op.value >= plan.operations.size()) return {};
      const auto& placement = plan.operations[origin.op.value];
      if ((placement.kind == EvaluationKind::kRoot || placement.kind == EvaluationKind::kPredicateSnapshot) &&
          placement.temporary) return EvaluationStorageLifetime::kInvocation;
    }
    return {};
  }, value.origin);
}
std::variant<std::uint64_t, EvaluationFailure> VerifyElidedPureEvaluations(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = EvaluationError;
  auto work = budget.work;
  const auto step = [&] {
    if (!work) throw Error::kWorkLimit;
    --work;
  };
  try {
    if (plan.flow != EvaluationFlow::kPhased || plan.operations.size() != graph.ops.size())
      return Error::kOwnership;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& placement = plan.operations[i];
      if (placement.kind != EvaluationKind::kElidedPure) continue;
      const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive || !CanErasePureOp(op) || placement.consumer || placement.temporary)
        return Error::kOwnership;
    }
    // Local closure certificate, O(ops + uses), independent of the builder's
    // backwards root worklist. A phi/catch copy remains a real observation.
    for (const auto& use : graph.uses) {
      step(); if (use.life != ValueLife::kLive) continue;
      const auto* value = std::get_if<ValueId>(&use.operand);
      const auto* result = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
      if (!result || plan.operations[result->op.value].kind != EvaluationKind::kElidedPure) continue;
      const auto* owner = std::get_if<OpId>(&use.owner);
      if (!owner || (plan.operations[owner->value].kind != EvaluationKind::kElidedPure &&
          !ForOfOperation(plan,*owner)))
        return Error::kOwnership;
    }
    return budget.work - work;
  } catch (Error error) { return error; }
}

std::variant<EvaluationLivenessCost, EvaluationFailure> ElidePureEvaluations(
    const analysis::ValueProgram& graph, EvaluationPlan& plan, analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = EvaluationError;
  try {
    auto left = budget;
    const auto step = [&] {
      if (!left.work) throw Error::kWorkLimit;
      --left.work;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      if (count > left.bytes / width) throw Error::kResourceLimit;
      left.bytes -= count * width;
    };
    if (plan.operations.size() != graph.ops.size()) return Error::kInvalidGraph;
    admit(graph.ops.size(), sizeof(std::uint8_t));
    admit(graph.ops.size(), sizeof(OpId));
    std::vector<std::uint8_t> retained(graph.ops.size());
    std::vector<OpId> pending;
    pending.reserve(graph.ops.size());
    admit(retained.capacity() - graph.ops.size(), sizeof(std::uint8_t));
    admit(pending.capacity() - graph.ops.size(), sizeof(OpId));
    const auto mark = [&](OpId id) {
      if (retained[id.value]) return;
      retained[id.value] = 1;
      pending.push_back(id);
    };
    const auto operand = [&](const ValueOperand& use) {
      if (const auto* id = std::get_if<ValueId>(&use))
        if (const auto* result = std::get_if<OpResult>(&graph.values[id->value].origin))
          mark(result->op);
    };
    // Classical backwards mark from observable roots, O(ops + uses). Every
    // phi input is conservatively observable to the current edge-copy plan.
    // Never drop a getter, coercion, call, throw, environment or control event.
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      if (graph.ops[i].life == ValueLife::kLive && !CanErasePureOp(graph.ops[i]))
        mark({static_cast<std::uint32_t>(i)});
    }
    for (const auto& use : graph.uses) {
      step();
      if (use.life == ValueLife::kLive && std::holds_alternative<ValueId>(use.owner))
        operand(use.operand);
    }
    for (std::size_t next = 0; next < pending.size(); ++next) {
      step();
      const auto& op = graph.ops[pending[next].value];
      for (std::uint32_t i = 0; i < op.use_count; ++i) {
        step(); operand(graph.uses[op.first_use.value + i].operand);
      }
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      if (graph.ops[i].life == ValueLife::kLive && !retained[i])
        plan.operations[i].kind = EvaluationKind::kElidedPure;
    }
    return EvaluationLivenessCost{budget.bytes - left.bytes, budget.work - left.work};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
