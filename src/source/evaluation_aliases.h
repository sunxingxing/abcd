#ifndef ABCD_SOURCE_EVALUATION_ALIASES_H_
#define ABCD_SOURCE_EVALUATION_ALIASES_H_

#include "evaluation_plan.h"

namespace abcd::source {
struct EvaluationAliasCost {
  std::size_t bytes;
  std::uint64_t work;
};
// Internal, after fresh availability/use checking and liveness projection.
// Changes only the local plan under construction; discard it on failure.
[[nodiscard]] std::variant<EvaluationAliasCost, EvaluationError> BuildEvaluationAliases(
    const analysis::ValueProgram& graph, EvaluationPlan& plan, analysis::SemanticBudget budget);
// Checks local copy equations and flat roots, not a replay of the builder.
[[nodiscard]] std::variant<std::uint64_t, EvaluationError> VerifyEvaluationAliases(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::SemanticBudget budget);
}  // namespace abcd::source
#endif
