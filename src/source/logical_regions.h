#ifndef ABCD_SOURCE_LOGICAL_REGIONS_H_
#define ABCD_SOURCE_LOGICAL_REGIONS_H_

#include "control_plan.h"
#include "abcd/source/source_tree.h"

namespace abcd::source {
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
    const analysis::ValueProgram& graph, const ControlPlan& control);
}  // namespace abcd::source
#endif
