#include "control_plan.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>

#include "control_skeleton.h"

namespace abcd::source {
namespace {
using Error = ControlError;
void Require(bool condition, Error error) {
  if (!condition) throw error;
}
struct RegionTask { std::uint32_t first; std::uint32_t stop; };
class ControlBuilder {
 public:
  ControlBuilder(ControlGraphView graph, std::span<const ControlId> entries, ControlQuota& quota)
      : graph_(graph), entries_(entries), quota_(quota), exit_(graph.NodeCount()),
        levels_(std::bit_width(exit_)) {}
  ControlPlan Build() {
    Require(exit_ != 0 && exit_ < std::numeric_limits<std::uint32_t>::max(), Error::kInvalidGraph);
    quota_.Size(indegree_, exit_);
    quota_.Size(order_, exit_);
    quota_.Size(parent_, exit_ + 1);
    quota_.Size(depth_, exit_ + 1);
    Require(levels_ != 0 && exit_ + 1 <= std::numeric_limits<std::size_t>::max() / levels_,
            Error::kResourceLimit);
    quota_.Size(up_, (exit_ + 1) * levels_);
    quota_.Size(owned_, exit_);
    quota_.Size(terminal_corridor_, exit_);
    quota_.Size(tasks_, exit_);
    plan_.view = graph_.Kind();
    plan_.revision = graph_.Revision();
    plan_.cfg_revision = graph_.CfgRevision();
    Require(!entries_.empty() && entries_.size() <= exit_, Error::kInvalidGraph);
    plan_.entry = entries_.front();
    quota_.Size(plan_.nodes, exit_);
    quota_.Size(plan_.edges, graph_.ArcCount());
    Order();
    Postdominators();
    Regions();
    plan_.allocated_bytes = plan_.nodes.capacity() * sizeof(ControlNode) +
                            plan_.edges.capacity() * sizeof(PlacedEdge);
    return std::move(plan_);
  }
 private:
  void Order() {
    std::size_t next_edge = 0;
    for (std::size_t b = 0; b < exit_; ++b) {
      quota_.Work();
      const auto& block = graph_.Node({static_cast<std::uint32_t>(b)});
      Require(block.first_arc.value == next_edge && block.arc_count <= analysis::kMaxNormalSuccessors &&
          block.arc_count <= graph_.ArcCount() - next_edge, Error::kInvalidGraph);
      for (std::uint8_t e = 0; e < block.arc_count; ++e) {
        quota_.Work();
        const auto& edge = graph_.Arc({static_cast<std::uint32_t>(next_edge++)});
        Require(edge.source.value == b && edge.successor == e && edge.target.value < exit_ &&
                (edge.kind == analysis::FlowEdgeKind::kTaken ||
                 edge.kind == analysis::FlowEdgeKind::kFallthrough), Error::kInvalidGraph);
        ++indegree_[edge.target.value];
      }
      plan_.nodes[b].block = {static_cast<std::uint32_t>(b)};
    }
    Require(next_edge == graph_.ArcCount(), Error::kInvalidGraph);
    std::size_t count = 0;
    for (std::size_t b = 0; b < exit_; ++b) {
      quota_.Work();
      if (indegree_[b] == 0) order_[count++] = b;
    }
    for (std::size_t i = 0; i < count; ++i) {
      quota_.Work();
      const auto& block = graph_.Node({order_[i]});
      for (std::uint8_t e = 0; e < block.arc_count; ++e) {
        quota_.Work();
        const auto target = graph_.Arc({block.first_arc.value + e}).target.value;
        if (--indegree_[target] == 0) order_[count++] = target;
      }
    }
    Require(count == exit_, Error::kCycle);
  }
  std::uint32_t Ancestor(std::uint32_t node, std::size_t level) const {
    return up_[static_cast<std::size_t>(node) * levels_ + level];
  }
  std::uint32_t Common(std::uint32_t a, std::uint32_t b) {
    if (depth_[a] < depth_[b]) std::swap(a, b);
    auto distance = depth_[a] - depth_[b];
    for (std::size_t bit = 0; distance; ++bit, distance >>= 1) {
      quota_.Work();
      if (distance & 1U) a = Ancestor(a, bit);
    }
    if (a == b) return a;
    for (auto bit = levels_; bit != 0; --bit) {
      quota_.Work();
      if (Ancestor(a, bit - 1) != Ancestor(b, bit - 1)) {
        a = Ancestor(a, bit - 1); b = Ancestor(b, bit - 1);
      }
    }
    return parent_[a];
  }
  void Postdominators() {
    // A DAG permits the postdominator recurrence in one reverse topological
    // pass. Binary-lifted LCA avoids iterative dominance sets / per-block walks.
    // The virtual exit is a real additional tree node, not an absent-ID sentinel.
    parent_[exit_] = exit_;
    // Kahn consumed the counts. Restore them once, so a terminal corridor
    // cannot absorb a shared/cross-entry block while choosing an early exit.
    for (std::size_t e = 0; e < graph_.ArcCount(); ++e) {
      quota_.Work();
      ++indegree_[graph_.Arc({static_cast<std::uint32_t>(e)}).target.value];
    }
    for (std::size_t level = 0; level < levels_; ++level) {
      quota_.Work();
      up_[exit_ * levels_ + level] = exit_;
    }
    for (std::size_t i = exit_; i != 0; --i) {
      quota_.Work();
      const auto id = order_[i - 1];
      const auto& block = graph_.Node({id});
      terminal_corridor_[id] = indegree_[id] == 1 &&
          ((block.arc_count == 0 && block.termination == ControlTermination::kAbrupt) || (block.arc_count == 1 &&
           terminal_corridor_[graph_.Arc(block.first_arc).target.value]));
      std::uint32_t join = exit_;
      for (std::uint8_t e = 0; e < block.arc_count; ++e) {
        quota_.Work();
        const auto target = graph_.Arc({block.first_arc.value + e}).target.value;
        join = e == 0 ? target : Common(join, target);
      }
      parent_[id] = join;
      depth_[id] = depth_[join] + 1;
      up_[static_cast<std::size_t>(id) * levels_] = join;
      for (std::size_t level = 1; level < levels_; ++level) {
        quota_.Work();
        up_[static_cast<std::size_t>(id) * levels_ + level] =
            Ancestor(Ancestor(id, level - 1), level - 1);
      }
    }
  }
  void Regions() {
    std::size_t pending = 0;
    for (const auto entry : entries_) {
      quota_.Work();
      Require(entry.value < exit_, Error::kInvalidGraph);
      tasks_[pending++] = {entry.value, static_cast<std::uint32_t>(exit_)};
    }
    std::size_t covered = 0;
    while (pending != 0) {
      quota_.Work();
      const auto task = tasks_[--pending];
      auto cursor = task.first;
      while (cursor != task.stop && cursor != exit_) {
        quota_.Work();
        Require(!owned_[cursor], Error::kCrossEntry);
        owned_[cursor] = 1;
        ++covered;
        auto& node = plan_.nodes[cursor];
        const auto& block = graph_.Node({cursor});
        if (block.arc_count == analysis::kMaxNormalSuccessors) {
          auto join = parent_[cursor];
          // Early-exit structuring: a private straight terminal arm does not
          // force the continuing arm under another if. Compute this summary
          // once in reverse order, not with a graph walk at each guard. Keep
          // bounded-region joins and two terminal arms unchanged.
          if (join == exit_ && task.stop == exit_) {
            const auto a = graph_.Arc(block.first_arc).target.value;
            const auto b = graph_.Arc({block.first_arc.value + 1}).target.value;
            if (terminal_corridor_[a] != terminal_corridor_[b])
              join = terminal_corridor_[a] ? b : a;
          }
          ControlArms arms;
          for (std::uint8_t e = 0; e < block.arc_count; ++e) {
            quota_.Work();
            const auto edge_id = block.first_arc.value + e;
            const auto& edge = graph_.Arc({edge_id});
            const bool taken = edge.kind == analysis::FlowEdgeKind::kTaken;
            plan_.edges[edge_id] = {{edge_id}, {cursor}, taken ? EdgePlacement::kTaken : EdgePlacement::kFallthrough};
            if (edge.target.value != join) {
              const ControlId child{edge.target.value};
              if (taken) arms.taken = child; else arms.fallthrough = child;
              Require(pending < tasks_.size(), Error::kResourceLimit);
              tasks_[pending++] = {child.value, join};
            }
          }
          node.arms = arms;
          if (join != task.stop && join != exit_) node.next = ControlId{join};
          cursor = join;
        } else if (block.arc_count == 1) {
          const auto edge_id = block.first_arc.value;
          const auto target = graph_.Arc({edge_id}).target.value;
          const bool joins = target == task.stop;
          plan_.edges[edge_id] = {{edge_id}, {cursor}, joins ? EdgePlacement::kJoin : EdgePlacement::kNext};
          if (!joins) node.next = ControlId{target};
          cursor = target;
        } else break;
      }
    }
    Require(covered == exit_, Error::kUncoveredBlock);
  }
  ControlGraphView graph_;
  std::span<const ControlId> entries_;
  ControlQuota& quota_;
  std::size_t exit_;
  std::size_t levels_;
  ControlPlan plan_;
  std::vector<std::uint32_t> indegree_, order_, parent_, depth_, up_;
  std::vector<std::uint8_t> owned_, terminal_corridor_;
  std::vector<RegionTask> tasks_;
};
}  // namespace

ControlPlan BuildControlSkeleton(const analysis::ValueProgram& graph,
    std::span<const ControlId> entries, ControlQuota& quota) {
  Require(graph.phases.empty(), Error::kInvalidGraph);
  return BuildControlSkeleton(ControlGraphView{graph}, entries, quota);
}
ControlPlan BuildControlSkeleton(ControlGraphView graph,
    std::span<const ControlId> entries, ControlQuota& quota) {
  return ControlBuilder(graph, entries, quota).Build();
}

std::variant<ControlPlan, ControlError> BuildControlPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  try {
    ControlQuota quota{budget};
    const ControlId entry{0};
    auto plan = [&] {
      try { return BuildControlSkeleton(graph, {&entry, 1}, quota); }
      catch (Error error) {
        if (error != Error::kCycle) throw;
        return BuildLoopControl(graph, quota);
      }
    }();
    const auto checked = VerifyControlPlan(graph, plan, quota.Remaining());
    if (const auto* error = std::get_if<ControlError>(&checked)) return *error;
    quota.Work(std::get<std::uint64_t>(checked));
    plan.work = budget.work - quota.RemainingWork();
    return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
