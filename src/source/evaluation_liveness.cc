#include "evaluation_liveness.h"

#include <new>
#include <stdexcept>
#include "../analysis/value_edits.h"

namespace abcd::source {
std::variant<std::uint64_t, EvaluationError> VerifyElidedPureEvaluations(
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
      if (!owner || plan.operations[owner->value].kind != EvaluationKind::kElidedPure)
        return Error::kOwnership;
    }
    return budget.work - work;
  } catch (Error error) { return error; }
}

std::variant<EvaluationLivenessCost, EvaluationError> ElidePureEvaluations(
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
