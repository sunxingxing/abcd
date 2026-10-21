#ifndef ABCD_SOURCE_EVALUATION_LIVENESS_H_
#define ABCD_SOURCE_EVALUATION_LIVENESS_H_

#include "evaluation_plan.h"

namespace abcd::source {
// Representation fact after original availability/phi-copy verification: a
// value already has stable source storage and can be read without replaying
// its producer. This is not dominance or permission to move its definition.
[[nodiscard]] bool HasRetainedEvaluationValue(const analysis::ValueProgram&,
    const EvaluationPlan&, analysis::ValueId);
// Lifetime of retained storage, not the exact lexical owner: private slots,
// native formals/receiver/self and phi storage survive the invocation. A native
// catch parameter survives only its handler; Value IR cannot widen that scope.
enum class EvaluationStorageLifetime : std::uint8_t { kInvocation, kHandler, kIteration };
[[nodiscard]] std::optional<EvaluationStorageLifetime> RetainedEvaluationLifetime(
    const analysis::ValueProgram&, const EvaluationPlan&, analysis::ValueId);
struct EvaluationLivenessCost {
  std::size_t bytes;
  std::uint64_t work;
};
// Internal: requires freshly verified availability/uses and a sized empty plan.
// Preserve the immutable input graph. All phi operands are roots, including
// exceptional snapshots; eliminating phis requires its own rewrite certificate.
[[nodiscard]] std::variant<EvaluationLivenessCost, EvaluationFailure> ElidePureEvaluations(
    const analysis::ValueProgram& graph, EvaluationPlan& plan, analysis::SemanticBudget budget);
// Independently check a proposed dead subgraph before its operands stop counting
// as literal observations. Requires fresh value availability/use integrity.
// No observable operation or retained/phi consumer may enter the dead closure.
[[nodiscard]] std::variant<std::uint64_t, EvaluationFailure> VerifyElidedPureEvaluations(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::SemanticBudget budget);
}  // namespace abcd::source
#endif
