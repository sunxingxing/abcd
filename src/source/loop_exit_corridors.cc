#include "control_skeleton.h"

namespace abcd::source {
namespace {
struct Interval {
  std::size_t begin = 0, end = 0;
};
struct Visit {
  LoopId loop;
  bool leave;
};
}  // namespace
void ExtendLoopExitCorridors(ControlGraphView graph,
                             std::span<const ControlNodeId> entries,
                             ControlLoopLayout& loops, ControlQuota& quota) {
  // A natural loop contains nodes on backedge paths, not necessarily the
  // single-entry code between a side exit and its shared destination. Absorb
  // only single-predecessor exit arms; never move code across a join,
  // independent entry or sibling/child loop. Private abrupt corridors can be
  // lexical early returns/throws; normal zero-arc ports must stay outside.
  // Only ancestor-owned
  // (or unowned) corridors can enter this lexical loop. No CFG edges change.
  // O(V+E+L), each newly owned node queues each outgoing edge once.
  std::vector<std::size_t> incoming, exits;
  std::vector<bool> root;
  std::vector<bool> abrupt;
  std::vector<bool> preserving;
  std::vector<std::optional<ControlNodeId>> predecessor;
  std::vector<ControlNodeId> terminals;
  std::vector<ControlArcId> pending;
  std::vector<std::optional<ControlArcId>> seeds, next;
  std::vector<std::optional<LoopId>> children, sibling;
  std::vector<Interval> intervals;
  std::vector<Visit> visits;
  quota.Size(incoming, graph.NodeCount());
  quota.Size(root, graph.NodeCount());
  quota.Size(abrupt, graph.NodeCount());
  quota.Size(preserving, graph.NodeCount());
  quota.Size(predecessor, graph.NodeCount());
  quota.Size(terminals, graph.NodeCount());
  quota.Size(exits, loops.loops.size());
  quota.Size(pending, graph.ArcCount() * 2);
  const auto loop_count = loops.loops.size();
  quota.Size(seeds, loop_count);
  quota.Size(next, graph.ArcCount());
  quota.Size(children, loop_count + 1);
  quota.Size(sibling, loop_count);
  quota.Size(intervals, loop_count);
  quota.Size(visits, loop_count * 2);
  for (std::uint32_t i = 0; i < loop_count; ++i) {
    quota.Work();
    const auto parent = loops.loops[i].parent;
    if (parent && (parent->value <= i || parent->value >= loop_count))
      throw ControlError::kInvalidGraph;
    const auto p = parent ? parent->value : loop_count;
    sibling[i] = children[p];
    children[p] = LoopId{i};
  }
  std::size_t size = 0, stamp = 0;
  for (auto root_loop = children[loop_count]; root_loop;
       root_loop = sibling[root_loop->value])
    visits[size++] = {*root_loop, false};
  while (size) {
    quota.Work();
    const auto visit = visits[--size];
    auto& interval = intervals[visit.loop.value];
    if (visit.leave) {
      interval.end = stamp;
      continue;
    }
    interval.begin = stamp++;
    visits[size++] = {visit.loop, true};
    for (auto child = children[visit.loop.value]; child;
         child = sibling[child->value])
      visits[size++] = {*child, false};
  }
  const auto outside = [&](LoopId loop, std::optional<LoopId> owner) {
    if (!owner) return true;
    return *owner != loop &&
           intervals[owner->value].begin <= intervals[loop.value].begin &&
           intervals[loop.value].begin < intervals[owner->value].end;
  };
  for (const auto entry : entries) {
    quota.Work();
    root[entry.value] = true;
  }
  std::size_t count = 0;
  for (std::uint32_t e = 0; e < graph.ArcCount(); ++e) {
    quota.Work();
    const ControlArcId id{e};
    const auto edge = graph.Arc(id);
    ++incoming[edge.target.value];
    predecessor[edge.target.value] = edge.source;
    const auto owner = loops.owners[edge.source.value];
    if (owner && outside(*owner, loops.owners[edge.target.value])) {
      ++exits[owner->value];
      next[e] = seeds[owner->value];
      seeds[owner->value] = id;
    }
  }
  // Summarize private linear terminal arms once, backwards. A shared join,
  // root, branch, or normal region port stops the summary. No per-loop walks.
  std::size_t terminal_count = 0;
  for (std::uint32_t b = 0; b < graph.NodeCount(); ++b) {
    quota.Work();
    const auto& node = graph.Node({b});
    if (!root[b] && incoming[b] == 1 && node.arc_count == 0 &&
        node.termination == ControlTermination::kAbrupt) {
      abrupt[b] = true;
      preserving[b] = node.completion == ControlCompletion::kPreserving;
      terminals[terminal_count++] = {b};
    }
  }
  for (std::size_t first = 0; first < terminal_count; ++first) {
    quota.Work();
    const auto parent = predecessor[terminals[first].value];
    if (!parent || root[parent->value] || incoming[parent->value] != 1 ||
        abrupt[parent->value] || graph.Node(*parent).arc_count != 1) continue;
    abrupt[parent->value] = true;
    preserving[parent->value] = preserving[terminals[first].value];
    terminals[terminal_count++] = *parent;
  }
  // Child-before-parent prevents an outer pass from claiming a corridor first
  // and then replaying it once per nested loop. Each node changes owner once.
  for (std::uint32_t l = 0; l < loop_count; ++l) {
    quota.Work();
    const LoopId loop{l};
    const auto begin = count;
    // If every exit ends privately in return/throw, retain one whole arm as
    // the loop's following continuation; the others belong inside the loop.
    // A typed preserving completion must remain following the cleanup loop;
    // header/latch shape and arc order cannot identify that semantic role.
    // Otherwise stable arc order chooses layout only. Absorb private abrupt
    // arm instead. Never split the retained arm at its empty phased Commit.
    std::optional<ControlArcId> following;
    bool all_abrupt = true, following_preserving = false;
    for (auto seed = seeds[l]; seed; seed = next[seed->value]) {
      quota.Work();
      const auto edge = graph.Arc(*seed);
      all_abrupt &= abrupt[edge.target.value];
      const bool preserve = preserving[edge.target.value];
      if (!following || (preserve && !following_preserving) ||
          (preserve == following_preserving && seed->value < following->value)) {
        following = seed; following_preserving = preserve;
      }
    }
    if (!all_abrupt && !following_preserving) following.reset();
    for (auto seed = seeds[l]; seed; seed = next[seed->value]) {
      // Distinct preserving exits remain distinct continuations. Let the
      // shared region builder handle/refuse multiple exits; never absorb one
      // into the loop just because another preserving arm sorts first.
      if (seed == following || preserving[graph.Arc(*seed).target.value]) continue;
      pending[count++] = *seed;
    }
    for (std::size_t first = begin; first < count; ++first) {
      quota.Work();
      const auto edge = graph.Arc(pending[first]);
      const auto owner = loops.owners[edge.source.value];
      const auto target = edge.target;
      const auto& node = graph.Node(target);
      if (owner != loop || exits[l] < 2 ||
          !outside(loop, loops.owners[target.value]) || root[target.value] ||
          incoming[target.value] != 1 ||
          (node.arc_count == 0 && node.termination != ControlTermination::kAbrupt))
        continue;
      loops.owners[target.value] = owner;
      for (std::uint8_t e = 0; e < node.arc_count; ++e)
        pending[count++] = {node.first_arc.value + e};
    }
  }
}
}  // namespace abcd::source
