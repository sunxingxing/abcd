#ifndef ABCD_SOURCE_FOR_IN_EVALUATION_H_
#define ABCD_SOURCE_FOR_IN_EVALUATION_H_
#include "evaluation_plan.h"
namespace abcd::source {
EvaluationFailure ForInEvaluationFailure(const analysis::ValueProgram&, ForInFailure);
std::variant<std::uint64_t, EvaluationFailure> VerifyForInEvaluation(
    const analysis::ValueProgram&, const EvaluationPlan&, analysis::SemanticBudget);
std::optional<EvaluationFailure> BuildForInEvaluation(
    const analysis::ValueProgram&, EvaluationPlan&, analysis::SemanticBudget&);
inline bool ForInOperation(const EvaluationPlan& p, analysis::OpId id) {
  return p.for_in && id.value < p.for_in->operations.size() && p.for_in->operations[id.value].has_value();
}
} // namespace abcd::source
#endif
