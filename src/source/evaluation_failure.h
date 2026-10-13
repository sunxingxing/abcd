#ifndef ABCD_SOURCE_EVALUATION_FAILURE_INTERNAL_H_
#define ABCD_SOURCE_EVALUATION_FAILURE_INTERNAL_H_
#include "evaluation_plan.h"
namespace abcd::source {
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
