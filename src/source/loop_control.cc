#include "control_skeleton.h"

#include <bit>
#include <limits>

namespace abcd::source {
namespace {
using Error = ControlError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct LoopInterval { std::uint32_t begin = 0; std::uint32_t end = 0; };
struct LoopVisit { LoopId loop; bool leave; };

class LoopControlBuilder {
 public:
  LoopControlBuilder(ControlGraphView graph, std::span<const ControlNodeId> roots,
      LoopForest forest, ControlQuota& quota)
      : graph_(graph), roots_(roots), forest_(std::move(forest)), quota_(quota) {}
  ControlPlan Build() {
    IndexLoops();
    Exits();
    Units();
    auto plan = BuildControlSkeleton(ControlGraphView{ControlProjection{
        graph_.Revision(), graph_.CfgRevision(), vertices_, links_}}, entries_, quota_);
    // The skeleton's synthetic adjacency is projected, but its published plan
    // indexes this builder's input view, with original arcs as the prefix.
    plan.view = graph_.Kind();
    for (std::size_t i = 0; i < plan.nodes.size(); ++i) {
      quota_.Work();
      plan.nodes[i].synthetic = synthetic_[i];
      if (const auto* wrapper = std::get_if<LoopControl>(&synthetic_[i]))
        plan.nodes[i].block = {forest_.loops[wrapper->loop.value].header.value};
      else if (const auto* transfer = std::get_if<LoopTransfer>(&synthetic_[i])) {
        if (const auto* edge = std::get_if<analysis::EdgeId>(&transfer->source))
          plan.nodes[i].block = {graph_.Arc({edge->value}).source.value};
        else plan.nodes[i].block = {forest_.loops[std::get<LoopId>(transfer->source).value].header.value};
      }
    }
    // Original blocks precede wrappers/transfer units, so their edge ranges
    // remain the exact original EdgeId prefix. Synthetic arcs are not ABC edges.
    plan.edges.resize(graph_.ArcCount());
    quota_.Size(plan.loops, forest_.loops.size());
    for (std::size_t i = 0; i < forest_.loops.size(); ++i) {
      quota_.Work();
      const auto& loop = forest_.loops[i];
      const auto exit = exits_[i] ? std::optional<analysis::WorkBlockId>{{exits_[i]->value}} : std::nullopt;
      plan.loops[i] = {{loop.header.value}, loop.parent, {loop.header.value}, Wrapper({static_cast<std::uint32_t>(i)}), exit};
    }
    plan.allocated_bytes += plan.loops.capacity() * sizeof(ControlLoop);
    return plan;
  }
 private:
  void IndexLoops() {
    const auto count = forest_.loops.size();
    Require(count != 0, Error::kCycle);
    Require(count <= std::numeric_limits<std::size_t>::max() / 2, Error::kResourceLimit);
    quota_.Size(intervals_, count);
    quota_.Size(heads_, count + 1);
    quota_.Size(siblings_, count);
    quota_.Size(visits_, count * 2);
    quota_.Size(exits_, count);
    quota_.Size(headers_, graph_.NodeCount());
    for (std::size_t i = 0; i < count; ++i) {
      quota_.Work();
      const auto& loop = forest_.loops[i];
      const auto parent = loop.parent ? loop.parent->value : count;
      Require(parent > i && parent <= count, Error::kInvalidGraph);
      siblings_[i] = heads_[parent]; heads_[parent] = LoopId{static_cast<std::uint32_t>(i)};
      headers_[loop.header.value] = LoopId{static_cast<std::uint32_t>(i)};
    }
    std::size_t pending = 0;
    for (auto root = heads_[count]; root; root = siblings_[root->value]) {
      quota_.Work();
      visits_[pending++] = {*root, false};
    }
    std::uint32_t stamp = 0;
    while (pending != 0) {
      quota_.Work();
      const auto visit = visits_[--pending];
      auto& interval = intervals_[visit.loop.value];
      if (visit.leave) { interval.end = stamp; continue; }
      interval.begin = stamp++;
      visits_[pending++] = {visit.loop, true};
      for (auto child = heads_[visit.loop.value]; child; child = siblings_[child->value]) {
        quota_.Work();
        Require(pending < visits_.size(), Error::kResourceLimit);
        visits_[pending++] = {*child, false};
      }
    }
    // Binary lifting locates the outermost crossed loop in logarithmic time;
    // never walk the entire ancestor chain separately for each exit edge.
    levels_ = std::bit_width(count);
    Require(count <= std::numeric_limits<std::size_t>::max() / levels_, Error::kResourceLimit);
    quota_.Size(ancestors_, count * levels_);
    for (std::size_t i = count; i-- > 0;) {
      quota_.Work(); ancestors_[i * levels_] = forest_.loops[i].parent;
      for (std::size_t level = 1; level < levels_; ++level) {
        quota_.Work(); const auto ancestor = ancestors_[i * levels_ + level - 1];
        if (ancestor) ancestors_[i * levels_ + level] = ancestors_[ancestor->value * levels_ + level - 1];
      }
    }
  }
  bool Inside(LoopId loop, ControlNodeId block) const {
    const auto owner = forest_.owners[block.value];
    if (!owner) return false;
    const auto& outer = intervals_[loop.value];
    return outer.begin <= intervals_[owner->value].begin && intervals_[owner->value].begin < outer.end;
  }
  void Exit(LoopId loop, ControlNodeId target) {
    auto& exit = exits_[loop.value];
    Require(!exit || *exit == target, Error::kLoopExits);
    exit = target;
  }
  LoopId ExitOwner(LoopId loop, ControlNodeId target) {
    Require(!Inside(loop, target), Error::kLoopExits);
    for (std::size_t level = levels_; level-- > 0;) {
      quota_.Work(); const auto ancestor = ancestors_[loop.value * levels_ + level];
      if (ancestor && !Inside(*ancestor, target)) loop = *ancestor;
    }
    return loop;
  }
  void Exits() {
    for (std::uint32_t i = 0; i < graph_.ArcCount(); ++i) {
      const auto edge = graph_.Arc({i});
      quota_.Work();
      const auto owner = forest_.owners[edge.source.value];
      // An edge to an enclosing header is an abrupt continue, not an inner
      // loop's normal successor. It must retain its exact outer owner.
      const auto header = headers_[edge.target.value];
      if (owner && header && Inside(*header, edge.source)) continue;
      // Assign a cross-nesting edge to the loop it actually exits. Intermediate
      // loops need no made-up fallthrough port: the AST emits break outer.
      if (owner && !Inside(*owner, edge.target)) Exit(ExitOwner(*owner, edge.target), edge.target);
    }
  }
  ControlId Wrapper(LoopId loop) const {
    return {static_cast<std::uint32_t>(graph_.NodeCount() + loop.value)};
  }
  ControlId Transfer(LoopId loop, LoopTransferKind kind, ControlTransferSource source) {
    Require(unit_count_ < synthetic_.size(), Error::kResourceLimit);
    const ControlId id{unit_count_++};
    synthetic_[id.value] = LoopTransfer{loop, kind, source};
    return id;
  }
  ControlId Target(ControlNodeId block, std::optional<LoopId> scope,
      ControlTransferSource source) {
    const auto header = headers_[block.value];
    if (scope && header && Inside(*header, forest_.loops[scope->value].header))
      return Transfer(*header, LoopTransferKind::kContinue, source);
    if (scope && !Inside(*scope, block)) {
      const auto owner = ExitOwner(*scope, block);
      Require(exits_[owner.value] == block, Error::kLoopExits);
      return Transfer(owner, LoopTransferKind::kBreak, source);
    }
    if (forest_.owners[block.value] == scope) return {block.value};
    const auto loop = headers_[block.value];
    Require(loop && forest_.loops[loop->value].parent == scope, Error::kCrossEntry);
    return Wrapper(*loop);
  }
  void Units() {
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    Require(forest_.loops.size() <= (kMaxId - graph_.NodeCount()) / 2 &&
        graph_.ArcCount() <= kMaxId - graph_.NodeCount() - forest_.loops.size() * 2,
        Error::kResourceLimit);
    const auto capacity = graph_.NodeCount() + forest_.loops.size() * 2 + graph_.ArcCount();
    quota_.Size(vertices_, capacity);
    quota_.Size(synthetic_, capacity);
    quota_.Size(links_, graph_.ArcCount() + forest_.loops.size());
    quota_.Size(entries_, forest_.loops.size() + roots_.size());
    unit_count_ = static_cast<std::uint32_t>(graph_.NodeCount() + forest_.loops.size());
    std::uint32_t edge_count = 0;
    for (std::uint32_t b = 0; b < graph_.NodeCount(); ++b) {
      quota_.Work();
      const auto& original = graph_.Node({b});
      auto& block = vertices_[b];
      block.first_arc = {edge_count}; block.arc_count = original.arc_count;
      for (std::uint8_t e = 0; e < original.arc_count; ++e) {
        quota_.Work();
        const auto& edge = graph_.Arc({edge_count});
        const auto target = Target(edge.target, forest_.owners[b], analysis::EdgeId{edge_count});
        links_[edge_count++] = {{b}, {target.value}, edge.kind, e};
      }
    }
    for (std::uint32_t i = 0; i < forest_.loops.size(); ++i) {
      quota_.Work();
      const LoopId loop{i};
      const auto id = Wrapper(loop);
      synthetic_[id.value] = LoopControl{loop};
      auto& block = vertices_[id.value];
      block.first_arc = {edge_count};
      if (exits_[i]) {
        const auto target = Target(*exits_[i], forest_.loops[i].parent, loop);
        links_[edge_count++] = {{id.value}, {target.value}, analysis::FlowEdgeKind::kTaken, 0};
        block.arc_count = 1;
      }
      entries_[i + roots_.size()] = {forest_.loops[i].header.value};
    }
    for (std::size_t i = graph_.NodeCount() + forest_.loops.size(); i < unit_count_; ++i) {
      quota_.Work(); vertices_[i].first_arc = {edge_count};
    }
    for (std::size_t i = 0; i < roots_.size(); ++i) {
      quota_.Work();
      const auto node = roots_[i];
      if (forest_.owners[node.value]) {
        const auto root = headers_[node.value];
        Require(root && !forest_.loops[root->value].parent, Error::kCrossEntry);
        entries_[i] = Wrapper(*root);
      } else entries_[i] = {node.value};
    }
    vertices_.resize(unit_count_); synthetic_.resize(unit_count_); links_.resize(edge_count);
  }
  ControlGraphView graph_;
  std::span<const ControlNodeId> roots_;
  LoopForest forest_;
  ControlQuota& quota_;
  std::vector<ControlVertex> vertices_;
  std::vector<ControlLink> links_;
  std::vector<LoopInterval> intervals_;
  std::vector<std::optional<LoopId>> heads_, siblings_, headers_;
  std::vector<std::optional<LoopId>> ancestors_;
  std::size_t levels_ = 0;
  std::vector<LoopVisit> visits_;
  std::vector<std::optional<ControlNodeId>> exits_;
  std::vector<ControlSynthetic> synthetic_;
  std::vector<ControlId> entries_;
  std::uint32_t unit_count_ = 0;
};
}  // namespace

ControlPlan BuildLoopControl(const analysis::ValueProgram& graph, ControlQuota& quota) {
  Require(graph.phases.empty(), Error::kInvalidGraph);
  constexpr ControlNodeId root{0};
  return BuildLoopControl(ControlGraphView{graph}, {&root, 1}, quota);
}
ControlPlan BuildLoopControl(ControlGraphView graph,
    std::span<const ControlNodeId> entries, ControlQuota& quota) {
  auto found = BuildLoopForest(graph, entries, quota.Remaining());
  if (const auto* error = std::get_if<LoopError>(&found)) {
    switch (*error) {
      case LoopError::kInvalidGraph: throw Error::kInvalidGraph;
      case LoopError::kUnreachable: throw Error::kUncoveredBlock;
      case LoopError::kIrreducible: throw Error::kIrreducible;
      case LoopError::kResourceLimit: throw Error::kResourceLimit;
      case LoopError::kWorkLimit: throw Error::kWorkLimit;
    }
  }
  auto forest = std::get<LoopForest>(std::move(found));
  quota.Work(forest.work); quota.Admit(forest.allocated_bytes, 1);
  quota.Admit(1, sizeof(LoopControlBuilder));
  return LoopControlBuilder(graph, entries, std::move(forest), quota).Build();
}
}  // namespace abcd::source
