#ifndef ABCD_SOURCE_PHASE_REFERENCE_H_
#define ABCD_SOURCE_PHASE_REFERENCE_H_

#include "evaluation_plan.h"

namespace abcd::source {
struct PhaseReferences {
  std::vector<std::optional<analysis::UseId>> consumers;
  std::size_t bytes = 0;
  std::uint64_t work = 0;
};
// Requires fresh phase availability, aliases and exceptional copy validation.
// Independently derives legal property-to-callee relocation through pure,
// unique fallthrough corridors. Never rewrites phases, SSA or copy schedules.
[[nodiscard]] std::variant<PhaseReferences, EvaluationError> CheckPhaseReferences(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget);
// Only after fresh evaluation verification; this is a lookup, not a proof.
[[nodiscard]] bool CrossPhaseReference(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, analysis::OpId property);
}  // namespace abcd::source
#endif
