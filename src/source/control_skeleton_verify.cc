#include "control_skeleton.h"

namespace abcd::source {
namespace {
using Error = ControlError;
void Require(bool valid, Error error) { if (!valid) throw error; }
struct RegionCheck { ControlId entry; std::optional<ControlId> stop; };
}
void VerifyControlSkeleton(ControlGraphView graph, std::span<const ControlId> entries,
    const ControlPlan& plan, ControlQuota& quota) {
  Require(plan.revision == graph.Revision() && plan.cfg_revision == graph.CfgRevision(), Error::kRevision);
  Require(plan.view == graph.Kind() && !entries.empty() && plan.entry == entries.front() && plan.loops.empty() &&
      plan.nodes.size() == graph.NodeCount() && plan.edges.size() == graph.ArcCount(), Error::kInvalidGraph);
  std::vector<std::uint8_t> seen;
  std::vector<RegionCheck> tasks;
  quota.Size(seen, plan.nodes.size()); quota.Size(tasks, plan.nodes.size());
  std::size_t pending = 0;
  const auto push = [&](RegionCheck task) {
    Require(pending < tasks.size(), Error::kResourceLimit);
    tasks[pending++] = task;
  };
  for (const auto entry : entries) { quota.Work(); push({entry, {}}); }
  std::size_t covered = 0, edge_count = 0;
  for (std::size_t b = 0; b < graph.NodeCount(); ++b) {
    quota.Work();
    const auto& block = graph.Node({static_cast<std::uint32_t>(b)});
    Require(block.first_arc.value == edge_count && block.arc_count <= analysis::kMaxNormalSuccessors &&
        block.arc_count <= graph.ArcCount() - edge_count, Error::kInvalidGraph);
    Require(plan.nodes[b].block.value == b && !plan.nodes[b].decision &&
        std::holds_alternative<std::monostate>(plan.nodes[b].synthetic), Error::kInvalidGraph);
    for (std::uint8_t e = 0; e < block.arc_count; ++e) {
      quota.Work(); const auto& edge = graph.Arc({static_cast<std::uint32_t>(edge_count++)});
      Require(edge.source.value == b && edge.successor == e && edge.target.value < graph.NodeCount() &&
          (edge.kind == analysis::FlowEdgeKind::kTaken || edge.kind == analysis::FlowEdgeKind::kFallthrough),
          Error::kInvalidGraph);
      if (block.arc_count == analysis::kMaxNormalSuccessors)
        Require(edge.kind == (e == 0 ? analysis::FlowEdgeKind::kTaken : analysis::FlowEdgeKind::kFallthrough),
            Error::kInvalidGraph);
    }
  }
  Require(edge_count == graph.ArcCount(), Error::kInvalidGraph);
  while (pending != 0) {
    quota.Work(); const auto task = tasks[--pending];
    std::optional<ControlId> cursor = task.entry;
    while (cursor) {
      quota.Work(); const auto id = *cursor;
      Require(id.value < seen.size() && !seen[id.value] && id != task.stop, Error::kCrossEntry);
      seen[id.value] = 1; ++covered;
      const auto& node = plan.nodes[id.value]; const auto& block = graph.Node({id.value});
      const auto join = node.next ? node.next : task.stop;
      if (node.next) Require(node.next->value < seen.size(), Error::kInvalidGraph);
      const auto arc = [&](std::uint8_t ordinal, std::optional<ControlId> target, EdgePlacement placement) {
        quota.Work();
        Require(target && target->value < seen.size(), Error::kInvalidGraph);
        const auto index = block.first_arc.value + ordinal;
        const auto& actual = plan.edges[index];
        Require(actual.edge.value == index && actual.owner == id && actual.placement == placement &&
            target->value == graph.Arc({index}).target.value, Error::kInvalidGraph);
      };
      if (block.arc_count == analysis::kMaxNormalSuccessors) {
        Require(node.arms.has_value(), Error::kInvalidGraph);
        const auto arm = [&](std::optional<ControlId> child, std::uint8_t ordinal, EdgePlacement placement) {
          if (child) { Require(child != join, Error::kCrossEntry); push({*child, join}); }
          arc(ordinal, child ? child : join, placement);
        };
        arm(node.arms->taken, 0, EdgePlacement::kTaken);
        arm(node.arms->fallthrough, 1, EdgePlacement::kFallthrough);
      } else if (block.arc_count == 1) {
        Require(!node.arms, Error::kInvalidGraph);
        arc(0, join, node.next ? EdgePlacement::kNext : EdgePlacement::kJoin);
      } else Require(!node.arms && !node.next, Error::kInvalidGraph);
      cursor = node.next;
    }
  }
  Require(covered == plan.nodes.size(), Error::kUncoveredBlock);
}
}  // namespace abcd::source
