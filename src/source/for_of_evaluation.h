#ifndef ABCD_SOURCE_FOR_OF_EVALUATION_H_
#define ABCD_SOURCE_FOR_OF_EVALUATION_H_
#include "evaluation_plan.h"
namespace abcd::source {
EvaluationFailure ForOfEvaluationFailure(const analysis::ValueProgram&,ForOfIterationFailure);
std::optional<EvaluationFailure> BuildForOfEvaluation(const analysis::ValueProgram&,
    const ForOfEvaluationInput*,EvaluationPlan&,analysis::SemanticBudget&);
std::variant<std::uint64_t,EvaluationFailure> VerifyForOfEvaluation(const analysis::ValueProgram&,
    const ForOfEvaluationInput*,const EvaluationPlan&,analysis::SemanticBudget);
inline bool ForOfOperation(const EvaluationPlan& p,analysis::OpId id) {
  return p.for_of && id.value<p.for_of->operations.size() && p.for_of->operations[id.value].has_value();
}
inline bool ForOfValue(const EvaluationPlan& p,analysis::ValueId id) {
  return p.for_of && id.value<p.for_of->values.size() && p.for_of->values[id.value].has_value();
}
}
#endif
