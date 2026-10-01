#ifndef ABCD_SOURCE_LOGICAL_REGIONS_H_
#define ABCD_SOURCE_LOGICAL_REGIONS_H_

#include "control_plan.h"
#include "abcd/source/source_tree.h"

namespace abcd::source {
class TreeCheckBudget;
struct EvaluationPlan;
// A value-selection diamond, not permission to move effects across its header.
// The expression planner must still own the RHS subtree, remove its phi copies,
// and preserve the order of every surrounding call/reference before emission.
struct LogicalRegion {
  TreeLogical operation;
  analysis::OpId boolean;
  analysis::OpId branch;
  analysis::ValueId merged;
  analysis::ValueId left;
  analysis::ValueOperand right;
  analysis::WorkBlockId header;
  std::optional<analysis::WorkBlockId> keep;
  analysis::WorkBlockId evaluate;
  analysis::WorkBlockId join;
  analysis::EdgeId keep_input;
  analysis::EdgeId evaluate_input;
};
// Requires verified simplified normal IR and its verified ControlPlan. Finds
// ToBoolean diamonds with a single result phi, including an empty keep arm.
// O(operations + values + uses + control nodes), with indexed block summaries;
// never scans the whole graph separately for each branch or phi.
[[nodiscard]] std::vector<LogicalRegion> FindLogicalRegions(
    const analysis::ValueProgram& graph, const ControlPlan& control, TreeCheckBudget* budget = nullptr);
// Select only regions whose entire RHS is one owned evaluation subtree.
[[nodiscard]] std::vector<LogicalRegion> FindLogicalExpressions(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, TreeCheckBudget& budget);
// Indexed eligible LHS producers; only the exact predicate/keep-edge pair is
// projected. Requires checked value uses and base evaluation placements. Each
// producer stays in its header, across no observable event/environment change.
[[nodiscard]] std::vector<std::optional<analysis::UseId>> FindLogicalLeftInputs(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation, TreeCheckBudget& budget);
}  // namespace abcd::source
#endif
