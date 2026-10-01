#include "control_skeleton.h"

namespace abcd::source {
void VerifyControlSkeleton(ControlGraphView graph, std::span<const ControlId> entries,
    const ControlPlan& plan, ControlQuota& quota) {
  if (!plan.loops.empty() || plan.nodes.size() != graph.NodeCount())
    throw ControlError::kInvalidGraph;
  // The same independent original-arc/ownership check covers acyclic decision
  // paths and loop composition. No builder or postdominator is rerun here.
  std::vector<ControlNodeId> roots;
  quota.Size(roots, entries.size());
  for (std::size_t i = 0; i < entries.size(); ++i) roots[i] = {entries[i].value};
  VerifyControlRegions(graph, roots, plan, quota);
}
}  // namespace abcd::source
