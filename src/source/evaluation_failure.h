#ifndef ABCD_SOURCE_EVALUATION_FAILURE_INTERNAL_H_
#define ABCD_SOURCE_EVALUATION_FAILURE_INTERNAL_H_
#include "evaluation_plan.h"
namespace abcd::source {
// A name-neutral initializer is not a proof of the second, ordered name
// mutation. Named private literals have a separate proof; computed keys and
// class-member projections cannot erase the name mutation.
inline std::optional<EvaluationFailure> NameSettingRefusal(
    const analysis::ValueProgram& graph, analysis::OpId id, bool projected = false) {
  const auto& op = graph.ops[id.value];
  if (op.life != analysis::ValueLife::kLive) return {};
  auto naming = analysis::PropertyNameSetting::kNone;
  if (projected) if (const auto* named = std::get_if<analysis::ValueOwnWrite>(&op.operation))
    naming = named->name_setting;
  // Named definitions are checked by LiteralPlan and the family naming proof.
  // Computed keys still require an independent key-domain proof.
  if (const auto* computed = std::get_if<analysis::ValueComputedOwnWrite>(&op.operation)) naming = computed->name_setting;
  if (naming == analysis::PropertyNameSetting::kNone) return {};
  EvaluationEvidence witness;
  witness.obligation = EvaluationObligation::kPlan;
  witness.location = OperationLocation(graph, id);
  witness.plan_cause = LiteralError::kNameSettingProof;
  return EvaluationFailure{EvaluationError::kUnsupportedOperation, witness};
}
inline std::optional<EvaluationFailure> CallSpreadRefusal(
    const analysis::ValueProgram& graph, analysis::OpId id, SourceContract contract) {
  const auto& op = graph.ops[id.value];
  const auto* call = std::get_if<analysis::ValueInvoke>(&op.operation);
  if (op.life != analysis::ValueLife::kLive || !call ||
      call->form != analysis::CallArgumentForm::kApplyArray) return {};
  if (CallSpread(contract) == CallSpreadSemantics::kPrivateArgumentsV1) return {};
  EvaluationEvidence witness;
  witness.location = OperationLocation(graph, id);
  witness.plan_cause = CallSpreadError::kPremise;
  return EvaluationFailure{EvaluationError::kUnsupportedOperation, witness};
}
// Only the ISA-layout ordinary wide call has the approved asm-stub deviation.
inline std::optional<EvaluationFailure> WideRangeRefusal(
    const analysis::ValueProgram& graph, analysis::OpId id, SourceContract contract) {
  const auto& op = graph.ops[id.value];
  const auto* call = std::get_if<analysis::ValueInvoke>(&op.operation);
  if (op.life != analysis::ValueLife::kLive || !call ||
      call->range_semantics != analysis::RangeCallSemantics::kIsaLayout ||
      WideRangeCalls(contract)) return {};
  EvaluationEvidence witness;
  witness.location = OperationLocation(graph, id);
  witness.input_check = EvaluationInputCheck::kWideRangePremise;
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
