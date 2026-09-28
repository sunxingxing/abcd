#include "exception_topology.h"

#include <algorithm>
#include <limits>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = ExceptionControlError;
void Require(bool valid, Error error = Error::kStructure) { if (!valid) throw error; }
struct Interval { std::size_t begin = 0, end = 0; };
struct Visit { LoopId loop; bool leave; };

// Try summaries participate in ordinary dominance/loop discovery. Their
// alternatives are NOT source branches: actual body/handler exits carry the
// transfer, and only a unique ordinary continuation survives on the wrapper.
class Composition {
 public:
  Composition(ExceptionTopology& topology, const ExceptionPlan& regions,
      const ValueProgram& graph, ControlQuota& quota)
      : t_(topology), regions_(regions), graph_(graph), quota_(quota) {}
  void Run() {
    Discover(); Inherit(); IndexLoops(); Route(); Compact();
  }
 private:
  void Discover() {
    std::size_t extra_nodes = 0, extra_arcs = 0;
    for (const auto& ports : t_.ports) {
      quota_.Work();
      if (ports.size() > 2) extra_nodes += ports.size() - 2;
      extra_arcs += ports.size() < 2 ? ports.size() : 2 * (ports.size() - 1);
    }
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    Require(extra_nodes < kMaxId - t_.blocks.size() &&
        extra_arcs < kMaxId - t_.edges.size(), Error::kResourceLimit);
    std::vector<ControlVertex> nodes;
    std::vector<ControlLink> arcs;
    std::vector<ControlNodeId> roots;
    quota_.Size(nodes, t_.blocks.size() + extra_nodes);
    quota_.Size(arcs, t_.edges.size() + extra_arcs);
    quota_.Size(roots, t_.entries.size());
    // Temporary adjacency slots allow fanout nodes to be appended without
    // changing the phase/wrapper prefix or depending on PC order.
    struct Successors { std::optional<ControlNodeId> first, second; };
    std::vector<Successors> summaries;
    quota_.Size(summaries, nodes.size());
    std::uint32_t next = static_cast<std::uint32_t>(t_.blocks.size());
    for (std::size_t r = 0; r < t_.ports.size(); ++r) {
      auto source = t_.mapping.regions[r].wrapper.value;
      const auto& ports = t_.ports[r];
      for (std::size_t p = 0; p < ports.size(); ++p) {
        quota_.Work(); summaries[source].first = ports[p].destination;
        if (p + 1 == ports.size()) break;
        if (p + 2 == ports.size()) {
          summaries[source].second = ports[++p].destination;
        } else {
          summaries[source].second = ControlNodeId{next}; source = next++;
        }
      }
    }
    Require(next == nodes.size());
    std::uint32_t edge = 0;
    for (std::uint32_t n = 0; n < nodes.size(); ++n) {
      quota_.Work(); nodes[n].first_arc = {edge};
      const auto add = [&](ControlNodeId target, FlowEdgeKind kind) {
        Require(edge < arcs.size());
        arcs[edge++] = {{n}, target, kind, nodes[n].arc_count++};
      };
      if (n < t_.blocks.size()) {
        const auto& block = t_.blocks[n];
        for (std::uint8_t e = 0; e < block.arc_count; ++e) {
          quota_.Work(); const auto& original = t_.edges[block.first_arc.value + e];
          add(original.target, original.kind);
        }
      }
      if (summaries[n].first) add(*summaries[n].first, FlowEdgeKind::kTaken);
      if (summaries[n].second) add(*summaries[n].second, FlowEdgeKind::kFallthrough);
    }
    Require(edge == arcs.size());
    for (std::size_t i = 0; i < roots.size(); ++i) roots[i] = {t_.entries[i].value};
    auto found = BuildLoopForest(ControlGraphView{ControlProjection{
        graph_.revision, graph_.cfg_revision, nodes, arcs}}, roots, quota_.Remaining());
    if (const auto* error = std::get_if<LoopError>(&found)) {
      switch (*error) {
        case LoopError::kResourceLimit: throw Error::kResourceLimit;
        case LoopError::kWorkLimit: throw Error::kWorkLimit;
        case LoopError::kInvalidGraph: case LoopError::kUnreachable:
        case LoopError::kIrreducible: throw Error::kStructure;
      }
    }
    auto forest = std::get<LoopForest>(std::move(found));
    quota_.Work(forest.work); quota_.Admit(forest.allocated_bytes, 1);
    for (const auto& loop : forest.loops) {
      quota_.Work(); Require(loop.header.value < t_.blocks.size());
    }
    t_.loops = {std::move(forest.loops), std::move(forest.owners)};
    ExtendLoopExitCorridors(ControlGraphView{ControlProjection{
        graph_.revision, graph_.cfg_revision, nodes, arcs}}, roots, t_.loops, quota_);
    t_.loops.owners.resize(t_.blocks.size());
  }
  void Inherit() {
    quota_.Size(inherited_, t_.ports.size());
    for (const auto region : regions_.layout.source_order) {
      quota_.Work(); const auto wrapper = t_.mapping.regions[region.value].wrapper;
      inherited_[region.value] = t_.loops.owners[wrapper.value];
      if (!inherited_[region.value]) {
        const auto parent = regions_.layout.regions[region.value].parent;
        if (parent) inherited_[region.value] = inherited_[parent->region.value];
      }
    }
    for (auto& loop : t_.loops.loops) {
      quota_.Work();
      if (!loop.parent && t_.owners[loop.header.value])
        loop.parent = inherited_[t_.owners[loop.header.value]->value];
    }
    for (std::size_t n = 0; n < t_.loops.owners.size(); ++n) {
      quota_.Work();
      if (!t_.loops.owners[n] && t_.owners[n])
        t_.loops.owners[n] = inherited_[t_.owners[n]->value];
    }
    // A contained arm's loops were discovered at independent roots. Restore
    // child-before-parent IDs once, in O(L), after lexical parent composition.
    const auto count = t_.loops.loops.size();
    std::vector<std::size_t> children;
    std::vector<LoopId> order, remap;
    quota_.Size(children, count); quota_.Size(order, count); quota_.Size(remap, count);
    for (const auto& loop : t_.loops.loops) {
      quota_.Work(); if (loop.parent) { Require(loop.parent->value < count); ++children[loop.parent->value]; }
    }
    std::size_t first = 0, last = 0;
    for (std::uint32_t i = 0; i < count; ++i) if (children[i] == 0) order[last++] = {i};
    while (first < last) {
      quota_.Work(); const auto id = order[first]; remap[id.value] = {static_cast<std::uint32_t>(first++)};
      const auto parent = t_.loops.loops[id.value].parent;
      if (parent && --children[parent->value] == 0) order[last++] = *parent;
    }
    Require(last == count);
    std::vector<NaturalLoop> loops;
    quota_.Size(loops, count);
    for (std::size_t i = 0; i < count; ++i) {
      quota_.Work(); loops[i] = t_.loops.loops[order[i].value];
      if (loops[i].parent) loops[i].parent = remap[loops[i].parent->value];
    }
    for (auto& owner : t_.loops.owners) { quota_.Work(); if (owner) owner = remap[owner->value]; }
    for (auto& owner : inherited_) { quota_.Work(); if (owner) owner = remap[owner->value]; }
    t_.loops.loops = std::move(loops);
    quota_.Size(t_.scoped_entries, t_.entries.size());
    for (std::size_t i = 0; i < t_.entries.size(); ++i) {
      quota_.Work(); const auto node = t_.entries[i];
      const auto region = t_.owners[node.value];
      t_.scoped_entries[i] = {{node.value}, region ? inherited_[region->value] : std::nullopt};
    }
  }
  void IndexLoops() {
    const auto count = t_.loops.loops.size();
    std::vector<std::optional<LoopId>> heads, siblings;
    std::vector<Visit> visits;
    quota_.Size(intervals_, count); quota_.Size(headers_, t_.blocks.size());
    quota_.Size(heads, count + 1); quota_.Size(siblings, count); quota_.Size(visits, 2 * count);
    for (std::uint32_t i = 0; i < count; ++i) {
      quota_.Work(); const auto& loop = t_.loops.loops[i];
      const auto parent = loop.parent ? loop.parent->value : count;
      siblings[i] = heads[parent]; heads[parent] = LoopId{i}; headers_[loop.header.value] = LoopId{i};
    }
    std::size_t pending = 0, stamp = 0;
    for (auto id = heads[count]; id; id = siblings[id->value]) visits[pending++] = {*id, false};
    while (pending != 0) {
      quota_.Work(); const auto visit = visits[--pending]; auto& interval = intervals_[visit.loop.value];
      if (visit.leave) { interval.end = stamp; continue; }
      interval.begin = stamp++; visits[pending++] = {visit.loop, true};
      for (auto id = heads[visit.loop.value]; id; id = siblings[id->value]) {
        quota_.Work(); Require(pending < visits.size()); visits[pending++] = {*id, false};
      }
    }
  }
  bool Contains(LoopId outer, std::optional<LoopId> inner) const {
    return inner && intervals_[outer.value].begin <= intervals_[inner->value].begin &&
        intervals_[inner->value].begin < intervals_[outer.value].end;
  }
  std::size_t Port(TryId region, WorkBlockId target) {
    const auto& ports = t_.ports[region.value];
    const auto found = std::lower_bound(ports.begin(), ports.end(), target,
        [&](const auto& port, auto key) { quota_.Work(); return port.target < key; });
    Require(found != ports.end() && found->target == target);
    return static_cast<std::size_t>(found - ports.begin());
  }
  void Route() {
    quota_.Size(destinations_, t_.blocks.size());
    quota_.Size(origins_, t_.blocks.size());
    std::vector<std::vector<bool>> abrupt;
    quota_.Size(abrupt, t_.ports.size());
    // Resolve a summary leaving nested tries using its parent's indexed port,
    // not an ancestor walk per CFG edge. Parent summaries precede children.
    for (const auto region : regions_.layout.source_order) {
      quota_.Work(); auto& ports = t_.ports[region.value];
      quota_.Size(abrupt[region.value], ports.size());
      const auto scope = inherited_[region.value];
      auto& planned = t_.mapping.regions[region.value];
      for (std::size_t p = 0; p < ports.size(); ++p) {
        quota_.Work(); auto& port = ports[p];
        const auto immediate = port.destination;
        if (const auto* exit = std::get_if<TryExitUnit>(&t_.mapping.units[immediate.value]))
          port.destination = t_.ports[exit->region.value][Port(exit->region, port.target)].destination;
        Require(!std::holds_alternative<TryExitUnit>(t_.mapping.units[port.destination.value]));
        const auto header = headers_[port.destination.value];
        const bool transfer = scope && ((header && Contains(*header, scope)) ||
            !Contains(*scope, t_.loops.owners[port.destination.value]));
        abrupt[region.value][p] = transfer;
        if (!transfer) {
          Require(!planned.exit, Error::kMultipleExits);
          planned.exit = port.target; destinations_[planned.wrapper.value] = immediate;
        }
      }
    }
    for (std::size_t n = 0; n < t_.mapping.units.size(); ++n) {
      quota_.Work(); const auto* exit = std::get_if<TryExitUnit>(&t_.mapping.units[n]);
      if (!exit) continue;
      const auto port = Port(exit->region, exit->target);
      if (!abrupt[exit->region.value][port]) continue;
      destinations_[n] = t_.ports[exit->region.value][port].destination;
      // Preserve the actual source loop for an exit inside a nested arm loop.
      // Normal ports remain arm terminals; abrupt ports become shared transfers.
      if (const auto* edge = std::get_if<EdgeId>(&exit->source)) {
        origins_[n] = *edge; t_.loops.owners[n] = t_.loops.owners[graph_.edges[edge->value].source.value];
      } else {
        const auto wrapper = t_.mapping.regions[std::get<TryId>(exit->source).value].wrapper;
        t_.loops.owners[n] = t_.loops.owners[wrapper.value];
      }
    }
  }
  void Compact() {
    std::vector<std::size_t> incoming;
    std::vector<std::optional<ControlNodeId>> remap;
    quota_.Size(incoming, t_.blocks.size()); quota_.Size(remap, t_.blocks.size());
    for (const auto& edge : t_.edges) { quota_.Work(); ++incoming[edge.target.value]; }
    for (const auto target : destinations_) { quota_.Work(); if (target) ++incoming[target->value]; }
    std::uint32_t count = 0;
    std::size_t arcs = t_.edges.size();
    for (std::size_t n = 0; n < remap.size(); ++n) {
      quota_.Work();
      // Only unused summary ports disappear, never real bytecode phases.
      const auto* exit = std::get_if<TryExitUnit>(&t_.mapping.units[n]);
      if (exit && incoming[n] == 0 && std::holds_alternative<TryId>(exit->source)) continue;
      remap[n] = ControlNodeId{count++}; if (destinations_[n]) ++arcs;
    }
    std::vector<ControlVertex> nodes;
    std::vector<ControlLink> edges;
    std::vector<ControlArc> origins;
    quota_.Size(nodes, count); quota_.Size(edges, arcs); quota_.Size(origins, arcs);
    std::uint32_t edge_count = 0;
    for (std::size_t n = 0; n < remap.size(); ++n) {
      quota_.Work(); if (!remap[n]) continue;
      const auto id = *remap[n]; auto& node = nodes[id.value]; node.first_arc = {edge_count};
      const auto add = [&](ControlNodeId target, FlowEdgeKind kind, ControlArc origin) {
        Require(remap[target.value].has_value());
        edges[edge_count] = {id, *remap[target.value], kind, node.arc_count++}; origins[edge_count++] = origin;
      };
      const auto old = t_.blocks[n];
      for (std::size_t e = 0; e < old.arc_count; ++e) {
        quota_.Work(); const auto index = old.first_arc.value + e;
        add(t_.edges[index].target, t_.edges[index].kind, t_.mapping.arcs[index]);
      }
      if (destinations_[n]) add(*destinations_[n], FlowEdgeKind::kTaken, {origins_[n]});
      t_.mapping.units[id.value] = t_.mapping.units[n];
      t_.loops.owners[id.value] = t_.loops.owners[n]; t_.owners[id.value] = t_.owners[n];
    }
    Require(edge_count == edges.size());
    // Headers and roots lie in the stable real-phase/try-wrapper prefix.
    t_.mapping.units.resize(count); t_.loops.owners.resize(count); t_.owners.resize(count);
    t_.blocks = std::move(nodes); t_.edges = std::move(edges); t_.mapping.arcs = std::move(origins);
  }
  ExceptionTopology& t_;
  const ExceptionPlan& regions_;
  const ValueProgram& graph_;
  ControlQuota& quota_;
  std::vector<std::optional<LoopId>> inherited_, headers_;
  std::vector<Interval> intervals_;
  std::vector<std::optional<ControlNodeId>> destinations_;
  std::vector<std::optional<EdgeId>> origins_;
};
}  // namespace
void ComposeExceptionLoops(ExceptionTopology& topology, const ExceptionPlan& regions,
    const ValueProgram& graph, ControlQuota& quota) {
  Composition(topology, regions, graph, quota).Run();
}
}  // namespace abcd::source
