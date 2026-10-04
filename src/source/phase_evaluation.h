#ifndef ABCD_SOURCE_PHASE_EVALUATION_H_
#define ABCD_SOURCE_PHASE_EVALUATION_H_

#include "evaluation_plan.h"

namespace abcd::source {
struct PhaseEvaluationCost {
  std::size_t bytes = 0;
  std::uint64_t work = 0;
};
// Requires fresh availability, aliases, LiteralPlan and exceptional copy validation.
// Discard the candidate plan on failure; original phases/SSA are never edited.
[[nodiscard]] std::variant<PhaseEvaluationCost, EvaluationError> BuildPhaseExpressions(
    const analysis::ValueProgram& graph, EvaluationPlan& plan, analysis::SemanticBudget budget);
// Checks proposed forest edges and ordered interval equations independently;
// does not rerun the builder's choice of which operands to retain as snapshots.
[[nodiscard]] std::variant<PhaseEvaluationCost, EvaluationError> VerifyPhaseExpressions(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::SemanticBudget budget);
// Source occurrence projection on an already validated graph and alias map.
[[nodiscard]] bool IsMemberReceiver(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, const analysis::OperandUse& use);
// Only after fresh evaluation verification; lookup, not a certificate. Includes
// the same owned cross-phase references in freshly checked literal nesting,
// and normal-flow closed pure values with independently checked single uses.
[[nodiscard]] bool CrossEvaluationInline(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, analysis::OpId producer);
}  // namespace abcd::source
#endif
