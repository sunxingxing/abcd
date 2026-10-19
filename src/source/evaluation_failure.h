#ifndef ABCD_SOURCE_EVALUATION_FAILURE_INTERNAL_H_
#define ABCD_SOURCE_EVALUATION_FAILURE_INTERNAL_H_
#include "evaluation_plan.h"
namespace abcd::source {
// A name-neutral initializer is not a proof of the second, ordered name
// mutation. Keep the new raw operations sealed until literal/name provenance
// and native NamedEvaluation are independently represented through recovery.
inline std::optional<EvaluationFailure> NameSettingRefusal(
    const analysis::ValueProgram& graph, analysis::OpId id) {
  const auto& op = graph.ops[id.value];
  if (op.life != analysis::ValueLife::kLive) return {};
  auto naming = analysis::PropertyNameSetting::kNone;
  if (const auto* named = std::get_if<analysis::ValueOwnWrite>(&op.operation)) naming = named->name_setting;
  if (const auto* computed = std::get_if<analysis::ValueComputedOwnWrite>(&op.operation)) naming = computed->name_setting;
  if (naming == analysis::PropertyNameSetting::kNone) return {};
  EvaluationEvidence witness;
  witness.obligation = EvaluationObligation::kPlan;
  witness.location = OperationLocation(graph, id);
  witness.plan_cause = LiteralError::kNameSettingProof;
  return EvaluationFailure{EvaluationError::kUnsupportedOperation, witness};
}
inline EvaluationEvidence EvaluationAt(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, analysis::OpId id, EvaluationObligation obligation) {
  EvaluationEvidence result; result.obligation = obligation;
  result.location = OperationLocation(graph, id);
  if (id.value < plan.operations.size()) {
    const auto& placement = plan.operations[id.value];
    result.placement = placement.kind; result.temporary = placement.temporary;
    result.actual_consumer = placement.consumer;
    if (placement.consumer && placement.consumer->value < graph.uses.size())
      if (const auto* owner = std::get_if<analysis::OpId>(&graph.uses[placement.consumer->value].owner))
        result.actual_owner = OperationLocation(graph, *owner);
  }
  return result;
}
} // namespace abcd::source
#endif
