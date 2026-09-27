#ifndef ABCD_SOURCE_EVALUATION_LIVENESS_H_
#define ABCD_SOURCE_EVALUATION_LIVENESS_H_

#include "evaluation_plan.h"

namespace abcd::source {
struct EvaluationLivenessCost {
  std::size_t bytes;
  std::uint64_t work;
};
// Internal: requires freshly verified availability/uses and a sized empty plan.
// Preserve the immutable input graph. All phi operands are roots, including
// exceptional snapshots; eliminating phis requires its own rewrite certificate.
[[nodiscard]] std::variant<EvaluationLivenessCost, EvaluationError> ElidePureEvaluations(
    const analysis::ValueProgram& graph, EvaluationPlan& plan, analysis::SemanticBudget budget);
}  // namespace abcd::source
#endif
