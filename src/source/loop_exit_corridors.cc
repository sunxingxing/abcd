#include "control_skeleton.h"

namespace abcd::source {
void ExtendLoopExitCorridors(ControlGraphView graph, std::span<const ControlNodeId> entries,
    ControlLoopLayout& loops, ControlQuota& quota) {
  // A natural loop contains nodes on backedge paths, not necessarily the
  // single-entry code between a side exit and its shared destination. Absorb
  // only linear single-predecessor corridors; never move code across a join,
  // independent entry, other loop, or terminal. No CFG edges are rewritten.
  // O(V+E+L), each newly owned node queues its sole successor once.
  std::vector<std::size_t> incoming, exits;
  std::vector<bool> root;
  std::vector<ControlArcId> pending;
  quota.Size(incoming, graph.NodeCount());
  quota.Size(root, graph.NodeCount());
  quota.Size(exits, loops.loops.size());
  quota.Size(pending, graph.ArcCount() + graph.NodeCount());
  for (const auto entry : entries) { quota.Work(); root[entry.value] = true; }
  std::size_t count = 0;
  for (std::uint32_t e = 0; e < graph.ArcCount(); ++e) {
    quota.Work(); const ControlArcId id{e}; const auto edge = graph.Arc(id);
    ++incoming[edge.target.value];
    const auto owner = loops.owners[edge.source.value];
    if (owner && !loops.loops[owner->value].parent && !loops.owners[edge.target.value]) {
      ++exits[owner->value]; pending[count++] = id;
    }
  }
  for (std::size_t first = 0; first < count; ++first) {
    quota.Work(); const auto edge = graph.Arc(pending[first]);
    const auto owner = loops.owners[edge.source.value];
    const auto target = edge.target;
    const auto& node = graph.Node(target);
    if (!owner || loops.loops[owner->value].parent || exits[owner->value] < 2 ||
        loops.owners[target.value] || root[target.value] || incoming[target.value] != 1 ||
        node.arc_count != 1) continue;
    loops.owners[target.value] = owner;
    pending[count++] = node.first_arc;
  }
}
}  // namespace abcd::source
