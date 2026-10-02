#include "control_plan.h"

#include <algorithm>
#include <array>
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
struct RegionTask { std::uint32_t first; std::uint32_t stop; bool retain_stop = false; };
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
    quota_.Size(decisions_, exit_);
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
    Decisions();
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
    // A DAG permits the follow-tree recurrence in one reverse topological
    // pass. Private abrupt arms do not constrain the continuing join: nested
    // guards may return/continue while their ordinary arms share a suffix.
    // Binary-lifted LCA avoids iterative sets / per-block walks. This lexical
    // choice is checked against every original arc, not a claim that the
    // chosen join postdominates paths which have already left the region.
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
      std::uint8_t continuing = 0;
      for (std::uint8_t e = 0; e < block.arc_count; ++e) {
        quota_.Work();
        const auto target = graph_.Arc({block.first_arc.value + e}).target.value;
        if (block.arc_count == analysis::kMaxNormalSuccessors && terminal_corridor_[target]) continue;
        join = continuing++ == 0 ? target : Common(join, target);
      }
      // When both arms are abrupt, retain their real common ancestry. They
      // still belong to this branch, rather than becoming fake continuations.
      if (continuing == 0 && block.arc_count != 0) {
        for (std::uint8_t e = 0; e < block.arc_count; ++e) {
          quota_.Work(); const auto target = graph_.Arc({block.first_arc.value + e}).target.value;
          join = e == 0 ? target : Common(join, target);
        }
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
  std::vector<std::optional<ControlId>> RegionalPredicates(std::span<const std::uint8_t> roots) {
    std::vector<std::optional<ControlId>> first_child, next_child, nearest, result;
    std::vector<std::optional<ControlArcId>> first_query, next_query;
    quota_.Size(first_child, exit_ + 1); quota_.Size(next_child, exit_);
    quota_.Size(nearest, exit_); quota_.Size(result, graph_.ArcCount());
    quota_.Size(first_query, exit_); quota_.Size(next_query, graph_.ArcCount());
    for (std::size_t i = 0; i < exit_; ++i) {
      quota_.Work(); const ControlId id{static_cast<std::uint32_t>(i)};
      next_child[i] = first_child[parent_[i]]; first_child[parent_[i]] = id;
      const auto node = graph_.Node({id.value});
      if (node.arc_count != analysis::kMaxNormalSuccessors) continue;
      for (std::uint8_t e = 0; e < node.arc_count; ++e) {
        quota_.Work(); const ControlArcId arc{node.first_arc.value + e};
        const auto evaluate = graph_.Arc(arc).target.value;
        if (indegree_[evaluate] != 1 || roots[evaluate]) continue;
        next_query[arc.value] = first_query[evaluate]; first_query[evaluate] = arc;
      }
    }
    // Walk the follow tree once. Each query asks for the nearest ancestor
    // predicate with an arc to its shared body. Rollback slots avoid per-node
    // ancestor walks, including deep sequences of conditional value diamonds.
    struct Frame {
      ControlId node;
      std::optional<ControlId> child;
      bool entered = false;
      std::array<std::optional<ControlId>, analysis::kMaxNormalSuccessors> previous{};
    };
    std::vector<Frame> stack; quota_.Size(stack, exit_ + 1);
    std::size_t depth = 1;
    stack[0] = {ControlId{static_cast<std::uint32_t>(exit_)}, first_child[exit_]};
    while (depth) {
      quota_.Work(); auto& frame = stack[depth - 1]; const auto id = frame.node.value;
      if (!frame.entered) {
        frame.entered = true;
        if (id < exit_) {
          const auto node = graph_.Node({id});
          if (node.arc_count == analysis::kMaxNormalSuccessors) {
            for (std::uint8_t e = 0; e < node.arc_count; ++e) {
              quota_.Work(); const auto target = graph_.Arc({node.first_arc.value + e}).target.value;
              frame.previous[e] = nearest[target]; nearest[target] = frame.node;
            }
          }
          for (auto query = first_query[id]; query; query = next_query[query->value]) {
            quota_.Work(); const auto edge = graph_.Arc(*query); const auto source = graph_.Node(edge.source);
            const auto shared = graph_.Arc({source.first_arc.value + (1U - edge.successor)}).target.value;
            result[query->value] = nearest[shared];
          }
        }
      }
      if (frame.child) {
        const auto child = *frame.child; frame.child = next_child[child.value];
        Require(depth < stack.size(), Error::kResourceLimit);
        stack[depth++] = {child, first_child[child.value]};
      } else {
        if (id < exit_) {
          const auto node = graph_.Node({id});
          if (node.arc_count == analysis::kMaxNormalSuccessors) {
            for (auto e = node.arc_count; e != 0; --e) {
              quota_.Work(); nearest[graph_.Arc({node.first_arc.value + e - 1}).target.value] = frame.previous[e - 1];
            }
          }
        }
        --depth;
      }
    }
    return result;
  }
  void Decisions() {
    // Reverse-topological path summaries cross Before/Commit and ordinary
    // linear evaluation without a suffix walk per branch. Independent roots
    // and shared predecessors stop ownership, even in a projected view.
    std::vector<std::optional<ControlId>> predicates;
    std::vector<std::uint8_t> roots;
    quota_.Size(predicates, exit_); quota_.Size(roots, exit_);
    for (const auto entry : entries_) {
      quota_.Work(); Require(entry.value < exit_, Error::kInvalidGraph);
      roots[entry.value] = 1;
    }
    const auto regional = RegionalPredicates(roots);
    for (std::size_t i = exit_; i != 0; --i) {
      quota_.Work(); const auto id = order_[i - 1];
      const auto& node = graph_.Node({id});
      if (node.arc_count == 1) {
        const auto target = graph_.Arc(node.first_arc).target.value;
        if (indegree_[target] == 1 && !roots[target]) predicates[id] = predicates[target];
      }
      if (node.arc_count != analysis::kMaxNormalSuccessors) continue;
      predicates[id] = ControlId{id};
      for (std::uint8_t e = 0; e < node.arc_count; ++e) {
        quota_.Work();
        const auto evaluate = graph_.Arc({node.first_arc.value + e}).target.value;
        const auto shared = graph_.Arc({node.first_arc.value + (1 - e)}).target.value;
        const auto candidate = regional[node.first_arc.value + e];
        if (indegree_[evaluate] != 1 || roots[evaluate] || !candidate) continue;
        const auto predicate = *candidate;
        const auto& child = graph_.Node({predicate.value});
        for (std::uint8_t c = 0; c < child.arc_count; ++c) {
          quota_.Work();
          if (graph_.Arc({child.first_arc.value + c}).target.value != shared) continue;
          const auto other = graph_.Arc({child.first_arc.value + (1 - c)}).target.value;
          if (other == shared) continue;
          const auto& tail = decisions_[predicate.value];
          // Both arms participate here: the lexical early-exit choice may be
          // the shared arm itself when the other arm terminates privately.
          // Retain the actual arm LCA, or the private other entry at virtual exit.
          const auto common = Common(shared, other);
          const auto join = tail && tail->evaluate.value == other &&
              tail->evaluate_on_taken == (c != 0) ? tail->join :
              ControlId{common == exit_ ? other : common};
          if (join.value == shared) continue; // This arm is an unconditional follow, not a decision body.
          decisions_[id] = ControlDecision{{evaluate}, e == 0, c == 0, join,
              predicate.value == evaluate ? std::nullopt : std::optional<ControlId>{predicate},
              predicates[evaluate] == predicate ? DecisionPrefix::kLinear : DecisionPrefix::kRegion};
        }
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
          if (decisions_[cursor]) {
            join = decisions_[cursor]->join.value;
            auto part = cursor;
            const auto& first = *decisions_[cursor];
            const auto shared = graph_.Arc({block.first_arc.value + (first.evaluate_on_taken ? 1U : 0U)}).target.value;
            while (true) {
              quota_.Work(); auto& planned = plan_.nodes[part];
              const auto& original = graph_.Node({part});
              const bool taken = part == cursor ? !first.evaluate_on_taken :
                  plan_.nodes[part].arms->taken.has_value();
              planned.arms = ControlArms{taken ? std::optional<ControlId>{{shared}} : std::nullopt,
                  taken ? std::nullopt : std::optional<ControlId>{{shared}}};
              planned.decision = decisions_[part];
              for (std::uint8_t e = 0; e < original.arc_count; ++e) {
                quota_.Work(); const auto edge = original.first_arc.value + e;
                plan_.edges[edge] = {{edge}, {part}, e == 0 ? EdgePlacement::kTaken : EdgePlacement::kFallthrough};
              }
              if (!planned.decision) {
                const auto other = graph_.Arc({original.first_arc.value + (taken ? 1U : 0U)}).target.value;
                if (other != join) {
                  if (taken) planned.arms->fallthrough = ControlId{other};
                  else planned.arms->taken = ControlId{other};
                  Require(pending < tasks_.size(), Error::kResourceLimit);
                  tasks_[pending++] = {other, join};
                }
                break;
              }
              auto next = planned.decision->evaluate.value;
              const auto predicate = planned.decision->predicate.value_or(planned.decision->evaluate).value;
              if (planned.decision->prefix == DecisionPrefix::kRegion) {
                Require(next != predicate, Error::kInvalidGraph);
                Require(pending < tasks_.size(), Error::kResourceLimit);
                tasks_[pending++] = {next, predicate, true}; next = predicate;
                Require(!owned_[next], Error::kCrossEntry);
              } else {
                Require(planned.decision->prefix == DecisionPrefix::kLinear, Error::kInvalidGraph);
                while (next != predicate) {
                  quota_.Work();
                  Require(!owned_[next] && indegree_[next] == 1, Error::kCrossEntry);
                  const auto& linear = graph_.Node({next});
                  Require(linear.arc_count == 1, Error::kInvalidGraph);
                  owned_[next] = 1; ++covered;
                  const auto target = graph_.Arc(linear.first_arc).target;
                  plan_.nodes[next].next = ControlId{target.value};
                  plan_.edges[linear.first_arc.value] = {{linear.first_arc.value}, {next}, EdgePlacement::kNext};
                  next = target.value;
                }
                Require(!owned_[next] && indegree_[next] == 1, Error::kCrossEntry);
              }
              owned_[next] = 1; ++covered;
              const bool next_taken = planned.decision->shared_on_taken;
              plan_.nodes[next].arms = ControlArms{next_taken ? std::optional<ControlId>{{shared}} : std::nullopt,
                  next_taken ? std::nullopt : std::optional<ControlId>{{shared}}};
              part = next;
            }
            Require(pending < tasks_.size(), Error::kResourceLimit);
            tasks_[pending++] = {shared, join};
            if (join != task.stop || task.retain_stop) node.next = ControlId{join};
            cursor = join;
            continue;
          }
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
          if ((join != task.stop || task.retain_stop) && join != exit_) node.next = ControlId{join};
          cursor = join;
        } else if (block.arc_count == 1) {
          const auto edge_id = block.first_arc.value;
          const auto target = graph_.Arc({edge_id}).target.value;
          const bool joins = target == task.stop;
          const bool follow = !joins || task.retain_stop;
          plan_.edges[edge_id] = {{edge_id}, {cursor}, follow ? EdgePlacement::kNext : EdgePlacement::kJoin};
          if (follow) node.next = ControlId{target};
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
  std::vector<std::optional<ControlDecision>> decisions_;
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
