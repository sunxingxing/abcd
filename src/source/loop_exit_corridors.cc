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
  // independent entry, sibling/child loop, or terminal. Only ancestor-owned
  // (or unowned) corridors can enter this lexical loop. No CFG edges change.
  // O(V+E+L), each newly owned node queues each outgoing edge once.
  std::vector<std::size_t> incoming, exits;
  std::vector<bool> root;
  std::vector<ControlArcId> pending;
  std::vector<std::optional<ControlArcId>> seeds, next;
  std::vector<std::optional<LoopId>> children, sibling;
  std::vector<Interval> intervals;
  std::vector<Visit> visits;
  quota.Size(incoming, graph.NodeCount());
  quota.Size(root, graph.NodeCount());
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
    const auto owner = loops.owners[edge.source.value];
    if (owner && outside(*owner, loops.owners[edge.target.value])) {
      ++exits[owner->value];
      next[e] = seeds[owner->value];
      seeds[owner->value] = id;
    }
  }
  // Child-before-parent prevents an outer pass from claiming a corridor first
  // and then replaying it once per nested loop. Each node changes owner once.
  for (std::uint32_t l = 0; l < loop_count; ++l) {
    quota.Work();
    const LoopId loop{l};
    const auto begin = count;
    for (auto seed = seeds[l]; seed; seed = next[seed->value])
      pending[count++] = *seed;
    for (std::size_t first = begin; first < count; ++first) {
      quota.Work();
      const auto edge = graph.Arc(pending[first]);
      const auto owner = loops.owners[edge.source.value];
      const auto target = edge.target;
      const auto& node = graph.Node(target);
      if (owner != loop || exits[l] < 2 ||
          !outside(loop, loops.owners[target.value]) || root[target.value] ||
          incoming[target.value] != 1 || node.arc_count == 0)
        continue;
      loops.owners[target.value] = owner;
      for (std::uint8_t e = 0; e < node.arc_count; ++e)
        pending[count++] = {node.first_arc.value + e};
    }
  }
}
}  // namespace abcd::source
