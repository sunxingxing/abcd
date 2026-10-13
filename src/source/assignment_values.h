#ifndef ABCD_SOURCE_ASSIGNMENT_VALUES_H_
#define ABCD_SOURCE_ASSIGNMENT_VALUES_H_
#include "evaluation_plan.h"
#include "tree_checks.h"
namespace abcd::source {
// Only the selected occurrence returns the value after its original [[Set]].
// Original SSA and store operations remain unchanged. Order/catch proofs still
// have to consume the complete reference/RHS/write subtree at that occurrence.
std::optional<analysis::OpId> AssignmentProducer(const EvaluationPlan&, analysis::UseId);
std::optional<analysis::OpId> EvaluationProducer(const analysis::ValueProgram&, const EvaluationPlan&, analysis::UseId);
void BuildAssignmentValues(const analysis::ValueProgram&, EvaluationPlan&, TreeCheckBudget&);
void VerifyAssignmentValues(const analysis::ValueProgram&, const EvaluationPlan&, TreeCheckBudget&);
}  // namespace abcd::source
#endif
