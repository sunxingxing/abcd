#include "control_plan.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>

#include "control_skeleton.h"
#include "control_forwarding.h"
#include "switch_region.h"

namespace abcd::source {
namespace {
using Error = ControlError;
void Require(bool condition, Error error) {
  if (!condition) throw error;
}
struct RegionTask { std::uint32_t first; std::uint32_t stop; bool retain_stop = false; std::optional<ControlArcId> incoming; };
struct DecisionTail { ControlId predicate, fallback; };
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
    quota_.Size(owners_, exit_);
    quota_.Size(private_terminal_, exit_);
    quota_.Size(straight_terminal_, exit_);
    quota_.Size(all_abrupt_, exit_);
    quota_.Size(shared_continuations_, exit_);
    quota_.Size(shared_terminal_, exit_);
    quota_.Size(closed_suffix_, exit_);
    quota_.Size(decisions_, exit_);
    quota_.Size(decision_tails_, exit_);
    quota_.Size(tasks_, exit_);
    plan_.view = graph_.Kind();
    plan_.revision = graph_.Revision();
    plan_.cfg_revision = graph_.CfgRevision();
    Require(!entries_.empty() && entries_.size() <= exit_, Error::kInvalidGraph);
    plan_.entry = entries_.front();
    quota_.Size(plan_.nodes, exit_);
    quota_.Size(plan_.edges, graph_.ArcCount());
    Order();
    NormalPostdominators();
    TerminalRegions();
    Postdominators();
    CompleteFollows();
    Decisions();
    // Preserve every successful layout. A duplicate in the deterministic
    // region census may expose a shared abrupt tail hidden by the old
    // single-predecessor summary. Refine follows before final verification;
    // never retry a failed verifier or delete/duplicate an original vertex.
    if (const auto conflict = Regions()) {
      if (!RefineSharedTerminals(conflict->control.node.node)) throw *conflict;
      for (std::size_t i = 0; i < exit_; ++i) {
        quota_.Work(); owned_[i] = 0; decisions_[i].reset();
        plan_.nodes[i] = ControlNode{{static_cast<std::uint32_t>(i)}, {}, {}, {}, {}};
      }
      Postdominators(); CompleteFollows(); Decisions();
      if (const auto refined = Regions()) throw *refined;
    }
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
  void TerminalRegions() {
    // Entry dominance is not lexical follow ancestry. On this already acyclic
    // view, predecessor LCA builds idom in topological order. A virtual root
    // also enters each scoped body/handler/loop entry. ReachMeet is the LCA of
    // every node reachable from a candidate. Equality proves that no reachable
    // suffix has another entry. AllAbrupt excludes normal try-exit ports and
    // nonterminating loop wrappers. Neither summary deletes any original arc;
    // the independent ownership verifier checks the final layout.
    std::vector<std::uint32_t> first, cursor, predecessors, dom_parent, meet;
    std::vector<std::uint8_t> roots;
    quota_.Size(first, exit_ + 1); quota_.Size(cursor, exit_);
    quota_.Size(predecessors, graph_.ArcCount());
    quota_.Size(dom_parent, exit_ + 1); quota_.Size(dom_depth_, exit_ + 1);
    quota_.Size(dom_up_, (exit_ + 1) * levels_); quota_.Size(meet, exit_);
    quota_.Size(roots, exit_);
    for (const auto entry : entries_) {
      quota_.Work(); Require(entry.value < exit_, Error::kInvalidGraph); roots[entry.value] = 1;
    }
    // Kahn consumed the indegrees. Restore once and build a predecessor CSR.
    for (std::uint32_t e = 0; e < graph_.ArcCount(); ++e) {
      quota_.Work(); ++indegree_[graph_.Arc({e}).target.value];
    }
    for (std::size_t i = 0; i < exit_; ++i) {
      quota_.Work(); first[i + 1] = first[i] + indegree_[i]; cursor[i] = first[i];
    }
    for (std::uint32_t e = 0; e < graph_.ArcCount(); ++e) {
      quota_.Work(); const auto edge = graph_.Arc({e}); predecessors[cursor[edge.target.value]++] = edge.source.value;
    }
    const auto ancestor = [&](std::uint32_t id, std::size_t level) { return dom_up_[id * levels_ + level]; };
    const auto common = [&](std::uint32_t a, std::uint32_t b) {
      if (dom_depth_[a] < dom_depth_[b]) std::swap(a, b);
      auto distance = dom_depth_[a] - dom_depth_[b];
      for (std::size_t bit = 0; distance; ++bit, distance >>= 1) {
        quota_.Work(); if (distance & 1U) a = ancestor(a, bit);
      }
      if (a == b) return a;
      for (auto bit = levels_; bit != 0; --bit) {
        quota_.Work();
        if (ancestor(a, bit - 1) != ancestor(b, bit - 1)) {
          a = ancestor(a, bit - 1); b = ancestor(b, bit - 1);
        }
      }
      return dom_parent[a];
    };
    dom_parent[exit_] = exit_;
    for (std::size_t level = 0; level < levels_; ++level) {
      quota_.Work(); dom_up_[exit_ * levels_ + level] = exit_;
    }
    for (const auto id : order_) {
      quota_.Work(); auto parent = static_cast<std::uint32_t>(exit_);
      if (!roots[id] && first[id] != first[id + 1]) {
        parent = predecessors[first[id]];
        for (auto p = first[id] + 1; p < first[id + 1]; ++p) {
          quota_.Work(); parent = common(parent, predecessors[p]);
        }
      }
      dom_parent[id] = parent; dom_depth_[id] = dom_depth_[parent] + 1;
      dom_up_[id * levels_] = parent;
      for (std::size_t level = 1; level < levels_; ++level) {
        quota_.Work(); dom_up_[id * levels_ + level] = ancestor(ancestor(id, level - 1), level - 1);
      }
    }
    std::vector<std::optional<ControlNodeId>> terminal;
    quota_.Size(terminal, exit_);
    for (std::size_t i = exit_; i-- > 0;) {
      quota_.Work(); const auto id = order_[i]; const auto block = graph_.Node({id});
      if (block.arc_count == 0 && block.termination == ControlTermination::kAbrupt)
        terminal[id] = ControlNodeId{id};
      else if (block.arc_count) {
        terminal[id] = terminal[graph_.Arc(block.first_arc).target.value];
        for (std::uint8_t e = 1; e < block.arc_count; ++e) {
          quota_.Work();
          if (terminal[id] != terminal[graph_.Arc({block.first_arc.value + e}).target.value])
            terminal[id].reset();
        }
      }
      meet[id] = id;
      all_abrupt_[id] = block.arc_count != 0 || block.termination == ControlTermination::kAbrupt;
      for (std::uint8_t e = 0; e < block.arc_count; ++e) {
        quota_.Work(); const auto target = graph_.Arc({block.first_arc.value + e}).target.value;
        all_abrupt_[id] = all_abrupt_[id] && all_abrupt_[target]; meet[id] = common(meet[id], meet[target]);
      }
      closed_suffix_[id] = meet[id] == id;
      // A multi-entry tail may still belong to one enclosing decision.
      // ReachMeet proves that every descendant is dominated by this tail;
      // the single terminal is a real abrupt port, never a normal try exit.
      // Its immediate dominator supplies the common decision boundary; one
      // arm reaches it on every path (also through split/conditional prefixes). All
      // incoming arcs remain in the layout and are checked independently.
      if (!roots[id] && indegree_[id] > 1 && terminal[id] && meet[id] == id &&
          dom_parent[id] < exit_) {
        const auto owner = graph_.Node({dom_parent[id]});
        for (std::uint8_t e = 0; e < owner.arc_count; ++e) {
          quota_.Work();
          const auto arm = graph_.Arc({owner.first_arc.value + e}).target.value;
          if (TreeCommon(id, arm, post_depth_, post_up_) == id)
            shared_terminal_[id] = 1;
        }
      }
      straight_terminal_[id] = indegree_[id] == 1 &&
          ((block.arc_count == 0 && block.termination == ControlTermination::kAbrupt) ||
           (block.arc_count == 1 && straight_terminal_[graph_.Arc(block.first_arc).target.value]));
      private_terminal_[id] = !roots[id] && indegree_[id] == 1 && all_abrupt_[id] && meet[id] == id;
      if (block.arc_count == analysis::kMaxNormalSuccessors && all_abrupt_[id]) {
        const auto a = graph_.Arc(block.first_arc).target.value;
        const auto b = graph_.Arc({block.first_arc.value + 1}).target.value;
        // Private(t) and !Dominates(branch, ReachMeet(c)): c reaches a normal
        // successor also reachable from outside this branch. Every node in t
        // is dominated by t, so t cannot reach that shared successor. Two
        // privately terminal arms cannot satisfy this predicate.
        if (private_terminal_[a] && common(id, meet[b]) != id) shared_continuations_[id] = ControlId{b};
        else if (private_terminal_[b] && common(id, meet[a]) != id) shared_continuations_[id] = ControlId{a};
      }
    }
  }
  bool RefineSharedTerminals(std::uint32_t continuation) {
    bool found = false;
    for (std::uint32_t id = 0; id < exit_; ++id) {
      quota_.Work();
      // The disputed continuation must remain ordinary control. A terminal
      // suffix containing it cannot be reclassified as an early abrupt arm.
      // ReachMeet already proves suffix closure under dominance, so one LCA
      // query suffices; no per-candidate graph walks or effect duplication.
      if (shared_terminal_[id] && TreeCommon(id, continuation, dom_depth_, dom_up_) == id)
        shared_terminal_[id] = 0;
      found = found || shared_terminal_[id];
    }
    refined_ = found;
    return found;
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
    for (std::size_t level = 0; level < levels_; ++level) {
      quota_.Work();
      up_[exit_ * levels_ + level] = exit_;
    }
    for (std::size_t i = exit_; i != 0; --i) {
      quota_.Work();
      const auto id = order_[i - 1];
      const auto& block = graph_.Node({id});
      std::uint32_t join = exit_;
      std::uint8_t continuing = 0;
      for (std::uint8_t e = 0; e < block.arc_count; ++e) {
        quota_.Work();
        const auto target = graph_.Arc({block.first_arc.value + e}).target.value;
        if (block.arc_count == analysis::kMaxNormalSuccessors &&
            (straight_terminal_[target] || (refined_ && shared_terminal_[target]))) continue;
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
  void CompleteFollows() {
    // Layout preference and ownership are distinct. Freeze every concrete
    // follow selected by the existing straight-terminal rule. Only a virtual
    // exit can gain a continuation from the private-region proof. This is a
    // deterministic extension of the follow tree, not a failed-layout retry.
    // A successor's refined ancestry may expose a formerly hidden shared tail.
    // Normal try-exit ports are not abrupt continuations: do not move a port
    // across its enclosing branch using an abrupt-region proof.
    for (std::size_t i = exit_; i-- > 0;) {
      quota_.Work(); const auto id = order_[i]; const auto block = graph_.Node({id});
      auto join = parent_[id];
      if (join == exit_ && all_abrupt_[id] && block.arc_count == analysis::kMaxNormalSuccessors) {
        const auto a = graph_.Arc(block.first_arc).target.value;
        const auto b = graph_.Arc({block.first_arc.value + 1}).target.value;
        join = Common(a, b);
        if (join == exit_ && shared_continuations_[id]) join = shared_continuations_[id]->value;
      }
      parent_[id] = join; depth_[id] = depth_[join] + 1;
      up_[static_cast<std::size_t>(id) * levels_] = join;
      for (std::size_t level = 1; level < levels_; ++level) {
        quota_.Work(); up_[static_cast<std::size_t>(id) * levels_ + level] =
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
      std::array<std::size_t, analysis::kMaxNormalSuccessors> previous_level{};
      std::optional<std::size_t> previous_statement;
    };
    // A statement-only unit (try wrapper/exit, protocol close) between the
    // evaluated node and its predicate cannot be a condition prefix: the
    // query keeps no regional predicate and planning stays ordinary.
    std::vector<std::size_t> nearest_level; quota_.Size(nearest_level, exit_);
    std::optional<std::size_t> statement_level;
    std::vector<Frame> stack; quota_.Size(stack, exit_ + 1);
    std::size_t depth = 1;
    stack[0] = {ControlId{static_cast<std::uint32_t>(exit_)}, first_child[exit_]};
    while (depth) {
      quota_.Work(); auto& frame = stack[depth - 1]; const auto id = frame.node.value;
      if (!frame.entered) {
        frame.entered = true;
        frame.previous_statement = statement_level;
        if (id < exit_) {
          const auto node = graph_.Node({id});
          if (node.statement) statement_level = depth - 1;
          if (node.arc_count == analysis::kMaxNormalSuccessors) {
            for (std::uint8_t e = 0; e < node.arc_count; ++e) {
              quota_.Work(); const auto target = graph_.Arc({node.first_arc.value + e}).target.value;
              frame.previous[e] = nearest[target]; nearest[target] = frame.node;
              frame.previous_level[e] = nearest_level[target]; nearest_level[target] = depth - 1;
            }
          }
          for (auto query = first_query[id]; query; query = next_query[query->value]) {
            quota_.Work(); const auto edge = graph_.Arc(*query); const auto source = graph_.Node(edge.source);
            const auto shared = graph_.Arc({source.first_arc.value + (1U - edge.successor)}).target.value;
            result[query->value] = nearest[shared];
            if (nearest[shared] && statement_level && *statement_level > nearest_level[shared])
              result[query->value].reset();
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
              quota_.Work(); const auto target = graph_.Arc({node.first_arc.value + e - 1}).target.value;
              nearest[target] = frame.previous[e - 1]; nearest_level[target] = frame.previous_level[e - 1];
            }
          }
        }
        statement_level = frame.previous_statement;
        --depth;
      }
    }
    return result;
  }
  std::uint32_t TreeCommon(std::uint32_t a, std::uint32_t b,
      std::span<const std::uint32_t> depths, std::span<const std::uint32_t> up) {
    const auto ancestor = [&](std::uint32_t node, std::size_t level) { return up[node * levels_ + level]; };
    if (depths[a] < depths[b]) std::swap(a, b);
    auto distance = depths[a] - depths[b];
    for (std::size_t bit = 0; distance; ++bit, distance >>= 1) {
      quota_.Work(); if (distance & 1U) a = ancestor(a, bit);
    }
    if (a == b) return a;
    for (auto bit = levels_; bit != 0; --bit) {
      quota_.Work();
      if (ancestor(a, bit - 1) != ancestor(b, bit - 1)) {
        a = ancestor(a, bit - 1); b = ancestor(b, bit - 1);
      }
    }
    return ancestor(a, 0);
  }
  void NormalPostdominators() {
    // Real all-normal-path postdominance, separate from both entry dominance
    // and the frozen lexical follow preference. Every sink reaches the virtual
    // exit; no abrupt arm is omitted. A normal/unknown port remains a port,
    // never an invented return. Standard DAG successor-LCA recurrence.
    quota_.Size(post_depth_, exit_ + 1);
    quota_.Size(post_up_, (exit_ + 1) * levels_);
    for (std::size_t level = 0; level < levels_; ++level) {
      quota_.Work(); post_up_[exit_ * levels_ + level] = exit_;
    }
    for (std::size_t i = exit_; i-- > 0;) {
      quota_.Work(); const auto id = order_[i]; const auto node = graph_.Node({id});
      auto parent = static_cast<std::uint32_t>(exit_);
      for (std::uint8_t e = 0; e < node.arc_count; ++e) {
        quota_.Work(); const auto target = graph_.Arc({node.first_arc.value + e}).target.value;
        parent = e == 0 ? target : TreeCommon(parent, target, post_depth_, post_up_);
      }
      post_depth_[id] = post_depth_[parent] + 1; post_up_[id * levels_] = parent;
      for (std::size_t level = 1; level < levels_; ++level) {
        quota_.Work(); post_up_[id * levels_ + level] =
            post_up_[post_up_[id * levels_ + level - 1] * levels_ + level - 1];
      }
    }
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
        // A refined lexical follow may skip an abrupt arm. It does not prove
        // that a regional decision prefix reaches its final predicate.
        if (refined_ && TreeCommon(predicate.value, evaluate, post_depth_, post_up_) != predicate.value)
          continue;
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
          // A private abrupt fallback cannot become the follow of a shared
          // continuing arm with an outside entry. Keep this outer guard as
          // ordinary if/else; the inner shared terminal decision remains one
          // region. This changes factoring, never the original arcs.
          if (refined_ && private_terminal_[other] && !closed_suffix_[shared]) continue;
          const auto common = Common(shared, other);
          const auto join = tail && tail->evaluate.value == other &&
              tail->evaluate_on_taken == (c != 0) ? tail->join :
              ControlId{common == exit_ ? other : common};
          if (join.value == shared) continue; // This arm is an unconditional follow, not a decision body.
          decisions_[id] = ControlDecision{{evaluate}, e == 0, c == 0, join,
              predicate.value == evaluate ? std::nullopt : std::optional<ControlId>{predicate},
              predicates[evaluate] == predicate ? DecisionPrefix::kLinear : DecisionPrefix::kRegion};
          decision_tails_[id] = tail ? decision_tails_[predicate.value] : DecisionTail{predicate, {other}};
        }
      }
    }
  }
  std::uint32_t DecisionJoin(std::uint32_t id, const RegionTask& task) {
    const auto& candidate = *decisions_[id];
    const auto old = candidate.join.value;
    const auto tail = decision_tails_[id];
    const auto node = graph_.Node({id});
    const auto shared = graph_.Arc({node.first_arc.value + (candidate.evaluate_on_taken ? 1U : 0U)}).target.value;
    // Only a proven incompatible preference is replaced, before any ownership
    // is committed. All other decisions retain their exact old choice. Prefix
    // entry/edge evidence comes from discovery; the independent region checker
    // rechecks every edge, scope and completion, not these summary flags.
    if (task.stop == exit_ || old != tail.fallback.value || !private_terminal_[old] ||
        TreeCommon(old, task.stop, dom_depth_, dom_up_) == old) return old;
    const auto refuse = [&](DecisionContinuationError reason, std::uint32_t witness) {
      // For this typed cause, current is the proposed arm and previous is the
      // enclosing region, not a claim that either has already acquired nodes.
      throw ControlFailure{Error::kCrossEntry, reason,
          {Location(witness), {}, {Location(shared), Location(old)}, Bounds(task),
           DecisionContinuationEvidence{Location(id), Location(tail.predicate.value), Location(shared),
               Location(tail.fallback.value), Location(old), Location(task.stop), Location(witness)}}};
    };
    if (TreeCommon(task.stop, shared, post_depth_, post_up_) == task.stop) {
      // Recheck each discovered condition's shared edge and the chain's final
      // fallback before committing this maximal decision. Prefix ownership is
      // certified independently by VerifyControlRegions before publication.
      auto part = id;
      while (decisions_[part]) {
        quota_.Work(); const auto& d = *decisions_[part]; const auto b = graph_.Node({part});
        if (graph_.Arc({b.first_arc.value + (d.evaluate_on_taken ? 1U : 0U)}).target.value != shared)
          refuse(DecisionContinuationError::kPrefix, part);
        const auto predicate = d.predicate.value_or(d.evaluate).value;
        const auto p = graph_.Node({predicate});
        if (p.arc_count != analysis::kMaxNormalSuccessors || indegree_[d.evaluate.value] != 1 ||
            graph_.Arc({p.first_arc.value + (d.shared_on_taken ? 0U : 1U)}).target.value != shared)
          refuse(DecisionContinuationError::kPrefix, predicate);
        if (!decisions_[predicate] && (predicate != tail.predicate.value ||
            graph_.Arc({p.first_arc.value + (d.shared_on_taken ? 1U : 0U)}).target.value != old))
          refuse(DecisionContinuationError::kPrefix, predicate);
        part = predicate;
      }
      return task.stop;
    }
    // Follow ancestry supplies a concrete reachable-stop witness, but does not
    // prove all paths reach it. Never substitute it for actual postdominance.
    if (Common(shared, task.stop) == task.stop) refuse(DecisionContinuationError::kPostdominator, task.stop);
    return old;
  }
  ControlLocation Location(std::uint32_t id) const { return {id, ControlOriginKind::kBlock, id}; }
  ControlRegionBounds Bounds(const RegionTask& task) const {
    return {Location(task.first), task.stop == exit_ ? std::nullopt :
        std::optional<ControlLocation>{Location(task.stop)}};
  }
  std::optional<ControlFailure> Own(std::uint32_t node, const RegionTask& task, std::optional<ControlArcId> arc) {
    if (owned_[node]) return ControlFailure{Error::kCrossEntry, ControlOwnershipError::kDuplicate,
        {Location(node), arc ? std::optional<std::uint32_t>{arc->value} : std::nullopt,
         Bounds(task), Bounds(owners_[node])}};
    owned_[node] = 1; owners_[node] = task;
    return {};
  }
  std::optional<ControlFailure> Regions() {
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
      auto incoming = task.incoming;
      while (cursor != task.stop && cursor != exit_) {
        quota_.Work();
        const auto decision_join = decisions_[cursor] ? DecisionJoin(cursor, task) : 0;
        // A continuation join equal to the shared target leaves the decision
        // with an empty shared arm, which the decision form cannot express.
        // Plan it as an ordinary conditional: an arm reaching the join is
        // omitted and the evaluated branch is planned as its own region.
        // Likewise when the join is the private terminal fallback but the
        // shared arm is not all-abrupt: its normal ports (a try's normal exit)
        // could only end inside a bounded arm, which is never a valid region.
        // Only when the shared target is the lexical follow does the ordinary
        // conditional own it exactly once, as its continuation.
        const auto shared_target = decisions_[cursor] ? graph_.Arc({graph_.Node({cursor}).first_arc.value +
            (decisions_[cursor]->evaluate_on_taken ? 1U : 0U)}).target.value : 0;
        const bool decision = decisions_[cursor] && decision_join != shared_target &&
            !(graph_.Kind() == ControlViewKind::kProjection && shared_target == parent_[cursor] &&
              decision_join == decision_tails_[cursor].fallback.value && !all_abrupt_[shared_target]);
        if (const auto failure = Own(cursor, task, incoming)) return failure;
        ++covered;
        auto& node = plan_.nodes[cursor];
        const auto& block = graph_.Node({cursor});
        if (block.arc_count == analysis::kMaxNormalSuccessors) {
          auto join = parent_[cursor];
          if (decision) {
            join = decision_join;
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
              if (planned.decision) planned.decision->join = {join};
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
                  tasks_[pending++] = {other, join, false, ControlArcId{original.first_arc.value + (taken ? 1U : 0U)}};
                }
                break;
              }
              auto next = planned.decision->evaluate.value;
              incoming = ControlArcId{original.first_arc.value + (planned.decision->evaluate_on_taken ? 0U : 1U)};
              const auto predicate = planned.decision->predicate.value_or(planned.decision->evaluate).value;
              if (planned.decision->prefix == DecisionPrefix::kRegion) {
                Require(next != predicate, Error::kInvalidGraph);
                Require(pending < tasks_.size(), Error::kResourceLimit);
                tasks_[pending++] = {next, predicate, true, ControlArcId{original.first_arc.value + (planned.decision->evaluate_on_taken ? 0U : 1U)}}; next = predicate; incoming.reset();
              } else {
                Require(planned.decision->prefix == DecisionPrefix::kLinear, Error::kInvalidGraph);
                while (next != predicate) {
                  quota_.Work();
                  Require(indegree_[next] == 1, Error::kCrossEntry);
                  const auto& linear = graph_.Node({next});
                  Require(linear.arc_count == 1, Error::kInvalidGraph);
                  if (const auto failure = Own(next, task, incoming)) return failure;
                  ++covered;
                  const auto target = graph_.Arc(linear.first_arc).target;
                  plan_.nodes[next].next = ControlId{target.value};
                  plan_.edges[linear.first_arc.value] = {{linear.first_arc.value}, {next}, EdgePlacement::kNext};
                  next = target.value; incoming = linear.first_arc;
                }
                Require(indegree_[next] == 1, Error::kCrossEntry);
              }
              if (const auto failure = Own(next, task, incoming)) return failure;
              ++covered;
              const bool next_taken = planned.decision->shared_on_taken;
              plan_.nodes[next].arms = ControlArms{next_taken ? std::optional<ControlId>{{shared}} : std::nullopt,
                  next_taken ? std::nullopt : std::optional<ControlId>{{shared}}};
              part = next;
            }
            Require(pending < tasks_.size(), Error::kResourceLimit);
            tasks_[pending++] = {shared, join, false, ControlArcId{block.first_arc.value + (first.evaluate_on_taken ? 1U : 0U)}};
            if (join != task.stop || task.retain_stop) node.next = ControlId{join};
            cursor = join; incoming.reset();
            continue;
          }
          // Early-exit structuring: a private terminal region does not
          // force the continuing arm under another if. Compute this summary
          // once in reverse order, not with a graph walk at each guard. Keep
          // bounded-region joins and two terminal arms unchanged.
          if (join == exit_ && task.stop == exit_) {
            const auto a = graph_.Arc(block.first_arc).target.value;
            const auto b = graph_.Arc({block.first_arc.value + 1}).target.value;
            if (straight_terminal_[a] != straight_terminal_[b])
              join = straight_terminal_[a] ? b : a;
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
              tasks_[pending++] = {child.value, join, false, ControlArcId{edge_id}};
            }
          }
          node.arms = arms;
          if ((join != task.stop || task.retain_stop) && join != exit_) node.next = ControlId{join};
          cursor = join; incoming.reset();
        } else if (block.arc_count == 1) {
          const auto edge_id = block.first_arc.value;
          const auto target = graph_.Arc({edge_id}).target.value;
          const bool joins = target == task.stop;
          const bool follow = !joins || task.retain_stop;
          plan_.edges[edge_id] = {{edge_id}, {cursor}, follow ? EdgePlacement::kNext : EdgePlacement::kJoin};
          if (follow) node.next = ControlId{target};
          cursor = target; incoming = ControlArcId{edge_id};
        } else break;
      }
    }
    Require(covered == exit_, Error::kUncoveredBlock);
    return {};
  }
  bool refined_ = false;
  ControlGraphView graph_;
  std::span<const ControlId> entries_;
  ControlQuota& quota_;
  std::size_t exit_;
  std::size_t levels_;
  ControlPlan plan_;
  std::vector<std::uint32_t> indegree_, order_, parent_, depth_, up_;
  std::vector<std::uint32_t> dom_depth_, dom_up_, post_depth_, post_up_;
  std::vector<std::uint8_t> owned_, shared_terminal_, closed_suffix_, private_terminal_, straight_terminal_, all_abrupt_;
  std::vector<RegionTask> tasks_, owners_;
  std::vector<std::optional<ControlDecision>> decisions_;
  std::vector<DecisionTail> decision_tails_;
  std::vector<std::optional<ControlId>> shared_continuations_;
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

ControlResult BuildControlPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  try {
    ControlQuota quota{budget};
    const ControlId entry{0};
    auto plan = [&] {
      try {
        try { return BuildControlSkeleton(graph, {&entry, 1}, quota); }
        catch (Error error) {
          if (error != Error::kCycle) throw;
          return BuildLoopControl(graph, quota);
        }
      } catch (const ControlFailure& failure) {
        try { return BuildForwardingControl(graph, failure, quota); }
        catch (const ControlFailure& remaining) { return BuildSwitchControl(graph, remaining, quota); }
      }
    }();
    const auto checked = VerifyControlPlan(graph, plan, quota.Remaining());
    if (const auto* error = std::get_if<ControlError>(&checked)) return *error;
    quota.Work(std::get<std::uint64_t>(checked));
    plan.work = budget.work - quota.RemainingWork();
    return plan;
  } catch (ControlFailure failure) {
    if (failure.control.decision) MapControlLocations(failure.control, [&](ControlLocation& location) {
      if (location.origin != ControlOriginKind::kBlock || location.source >= graph.blocks.size()) return;
      const auto& block = graph.blocks[location.source];
      if (!block.op_count || block.first_op.value >= graph.ops.size()) return;
      const auto& op = graph.ops[block.first_op.value];
      if (op.origin.value >= graph.origins.size()) return;
      const auto& origin = graph.origins[op.origin.value];
      location.working_pc = origin.pc; location.pc = origin.original.value_or(origin.pc);
    });
    return failure;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
