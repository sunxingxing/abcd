#ifndef ABCD_SOURCE_LOGICAL_EVALUATION_H_
#define ABCD_SOURCE_LOGICAL_EVALUATION_H_

#include "evaluation_plan.h"
#include "tree_checks.h"

namespace abcd::source {
// Named/computed getter snapshots can assign their retained private slot at
// their earliest strict-comparison use, with an independent local order proof.
void BuildPredicateSnapshots(const analysis::ValueProgram&, EvaluationPlan&, TreeCheckBudget&);
void VerifyPredicateSnapshots(const analysis::ValueProgram&, const EvaluationPlan&, TreeCheckBudget&);
// The builder starts from a freshly constructed local forest. Its backwards
// choices are checked by independent forward interval equations; the original
// local forest, conditional arms and all incoming copies remain checked too.
void BuildLogicalValues(const analysis::ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget);
void VerifyLogicalValues(const analysis::ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget);
void BuildPhaseSelectionCalls(const analysis::ValueProgram&, EvaluationPlan&, TreeCheckBudget&);
void VerifyPhaseSelectionCalls(const analysis::ValueProgram&, const EvaluationPlan&, TreeCheckBudget&);
// Only after fresh evaluation verification, which checks the contracted
// diamond's effect intervals and its exclusively owned incoming values.
bool CrossLogicalInline(const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::OpId producer);
// Local verification still checks the original forest at original block sites.
// The separate forward interval proof checks the proposed cross-region edges.
OpEvaluation BeforeLogicalFusion(const analysis::ValueProgram& graph, const EvaluationPlan& plan, std::size_t operation);
}  // namespace abcd::source
#endif
