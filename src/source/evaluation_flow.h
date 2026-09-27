#ifndef ABCD_SOURCE_EVALUATION_FLOW_H_
#define ABCD_SOURCE_EVALUATION_FLOW_H_

#include "evaluation_plan.h"
#include "../analysis/value_flow.h"

namespace abcd::source {
inline std::variant<std::uint64_t, EvaluationError> CheckEvaluationFlow(
    const analysis::ValueProgram& graph, EvaluationFlow flow, analysis::SemanticBudget budget) {
  switch (flow) {
    case EvaluationFlow::kNormal:
      if (!graph.phases.empty()) return EvaluationError::kUnsupportedOperation;
      return std::uint64_t{0};
    case EvaluationFlow::kPhased: {
      if (graph.phases.empty()) return EvaluationError::kUnsupportedOperation;
      const auto checked = analysis::VerifyPhasedValueFlow(graph, budget);
      if (const auto* proof = std::get_if<analysis::PhasedValueFlowProof>(&checked)) return proof->work;
      const auto error = std::get<analysis::ValueAvailabilityError>(checked);
      if (error == analysis::ValueAvailabilityError::kResourceLimit) return EvaluationError::kResourceLimit;
      if (error == analysis::ValueAvailabilityError::kWorkLimit) return EvaluationError::kWorkLimit;
      return EvaluationError::kUnavailableValue;
    }
  }
  return EvaluationError::kUnsupportedOperation;
}
// Explicit entry points share planning/verification, but never turn a phased
// graph into a normal graph by dropping its phase or exception information.
std::variant<std::uint64_t, EvaluationError> VerifyEvaluationForFlow(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget, EvaluationFlow flow);
}  // namespace abcd::source
#endif
