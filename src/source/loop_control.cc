#include "control_skeleton.h"

#include <limits>

namespace abcd::source {
namespace {
using Error = ControlError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct LoopInterval { std::uint32_t begin = 0; std::uint32_t end = 0; };
struct LoopVisit { LoopId loop; bool leave; };

class LoopControlBuilder {
 public:
  LoopControlBuilder(const analysis::ValueProgram& graph, LoopForest forest, ControlQuota& quota)
      : graph_(graph), forest_(std::move(forest)), quota_(quota) {}
  ControlPlan Build() {
    IndexLoops();
    Exits();
    Units();
    auto plan = BuildControlSkeleton(units_, entries_, quota_);
    for (std::size_t i = 0; i < plan.nodes.size(); ++i) {
      quota_.Work();
      plan.nodes[i].synthetic = synthetic_[i];
      if (const auto* wrapper = std::get_if<LoopControl>(&synthetic_[i]))
        plan.nodes[i].block = forest_.loops[wrapper->loop.value].header;
      else if (const auto* transfer = std::get_if<LoopTransfer>(&synthetic_[i])) {
        if (const auto* edge = std::get_if<analysis::EdgeId>(&transfer->source))
          plan.nodes[i].block = graph_.edges[edge->value].source;
        else plan.nodes[i].block = forest_.loops[std::get<LoopId>(transfer->source).value].header;
      }
    }
    // Original blocks precede wrappers/transfer units, so their edge ranges
    // remain the exact original EdgeId prefix. Synthetic arcs are not ABC edges.
    plan.edges.resize(graph_.edges.size());
    quota_.Size(plan.loops, forest_.loops.size());
    for (std::size_t i = 0; i < forest_.loops.size(); ++i) {
      quota_.Work();
      const auto& loop = forest_.loops[i];
      plan.loops[i] = {loop.header, loop.parent, {loop.header.value}, Wrapper({static_cast<std::uint32_t>(i)}), exits_[i]};
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
    quota_.Size(headers_, graph_.blocks.size());
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
  }
  bool Inside(LoopId loop, analysis::WorkBlockId block) const {
    const auto owner = forest_.owners[block.value];
    if (!owner) return false;
    const auto& outer = intervals_[loop.value];
    return outer.begin <= intervals_[owner->value].begin && intervals_[owner->value].begin < outer.end;
  }
  void Exit(LoopId loop, analysis::WorkBlockId target) {
    auto& exit = exits_[loop.value];
    Require(!exit || *exit == target, Error::kLoopExits);
    exit = target;
  }
  void Exits() {
    for (const auto& edge : graph_.edges) {
      quota_.Work();
      const auto owner = forest_.owners[edge.source.value];
      if (owner && !Inside(*owner, edge.target)) Exit(*owner, edge.target);
    }
    // Children precede parents. A single normal exit can be propagated once
    // per child instead of visiting all ancestors for every CFG edge.
    for (std::size_t i = 0; i < forest_.loops.size(); ++i) {
      quota_.Work();
      const auto parent = forest_.loops[i].parent;
      if (parent && exits_[i] && !Inside(*parent, *exits_[i])) Exit(*parent, *exits_[i]);
    }
  }
  ControlId Wrapper(LoopId loop) const {
    return {static_cast<std::uint32_t>(graph_.blocks.size() + loop.value)};
  }
  ControlId Transfer(LoopId loop, LoopTransferKind kind, ControlTransferSource source) {
    Require(unit_count_ < synthetic_.size(), Error::kResourceLimit);
    const ControlId id{unit_count_++};
    synthetic_[id.value] = LoopTransfer{loop, kind, source};
    return id;
  }
  ControlId Target(analysis::WorkBlockId block, std::optional<LoopId> scope,
      ControlTransferSource source) {
    if (scope && block == forest_.loops[scope->value].header)
      return Transfer(*scope, LoopTransferKind::kContinue, source);
    if (scope && !Inside(*scope, block)) {
      Require(exits_[scope->value] == block, Error::kLoopExits);
      return Transfer(*scope, LoopTransferKind::kBreak, source);
    }
    if (forest_.owners[block.value] == scope) return {block.value};
    const auto loop = headers_[block.value];
    Require(loop && forest_.loops[loop->value].parent == scope, Error::kCrossEntry);
    return Wrapper(*loop);
  }
  void Units() {
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    Require(forest_.loops.size() <= (kMaxId - graph_.blocks.size()) / 2 &&
        graph_.edges.size() <= kMaxId - graph_.blocks.size() - forest_.loops.size() * 2,
        Error::kResourceLimit);
    const auto capacity = graph_.blocks.size() + forest_.loops.size() * 2 + graph_.edges.size();
    quota_.Size(units_.blocks, capacity);
    quota_.Size(synthetic_, capacity);
    quota_.Size(units_.edges, graph_.edges.size() + forest_.loops.size());
    quota_.Size(entries_, forest_.loops.size() + 1);
    units_.revision = graph_.revision; units_.cfg_revision = graph_.cfg_revision;
    unit_count_ = static_cast<std::uint32_t>(graph_.blocks.size() + forest_.loops.size());
    std::uint32_t edge_count = 0;
    for (std::uint32_t b = 0; b < graph_.blocks.size(); ++b) {
      quota_.Work();
      const auto& original = graph_.blocks[b];
      auto& block = units_.blocks[b];
      block.first_edge = {edge_count}; block.edge_count = original.edge_count;
      for (std::uint8_t e = 0; e < original.edge_count; ++e) {
        quota_.Work();
        const auto& edge = graph_.edges[edge_count];
        const auto target = Target(edge.target, forest_.owners[b], analysis::EdgeId{edge_count});
        units_.edges[edge_count++] = {{b}, {target.value}, edge.kind, e};
      }
    }
    for (std::uint32_t i = 0; i < forest_.loops.size(); ++i) {
      quota_.Work();
      const LoopId loop{i};
      const auto id = Wrapper(loop);
      synthetic_[id.value] = LoopControl{loop};
      auto& block = units_.blocks[id.value];
      block.first_edge = {edge_count};
      if (exits_[i]) {
        const auto target = Target(*exits_[i], forest_.loops[i].parent, loop);
        units_.edges[edge_count++] = {{id.value}, {target.value}, analysis::FlowEdgeKind::kTaken, 0};
        block.edge_count = 1;
      }
      entries_[i + 1] = {forest_.loops[i].header.value};
    }
    for (std::size_t i = graph_.blocks.size() + forest_.loops.size(); i < unit_count_; ++i) {
      quota_.Work(); units_.blocks[i].first_edge = {edge_count};
    }
    if (forest_.owners[0]) {
      const auto root = headers_[0];
      Require(root && !forest_.loops[root->value].parent, Error::kCrossEntry);
      entries_[0] = Wrapper(*root);
    } else entries_[0] = {0};
    units_.blocks.resize(unit_count_); synthetic_.resize(unit_count_); units_.edges.resize(edge_count);
  }
  const analysis::ValueProgram& graph_;
  LoopForest forest_;
  ControlQuota& quota_;
  analysis::ValueProgram units_;
  std::vector<LoopInterval> intervals_;
  std::vector<std::optional<LoopId>> heads_, siblings_, headers_;
  std::vector<LoopVisit> visits_;
  std::vector<std::optional<analysis::WorkBlockId>> exits_;
  std::vector<ControlSynthetic> synthetic_;
  std::vector<ControlId> entries_;
  std::uint32_t unit_count_ = 0;
};
}  // namespace

ControlPlan BuildLoopControl(const analysis::ValueProgram& graph, ControlQuota& quota) {
  auto found = BuildLoopForest(graph, quota.Remaining());
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
  return LoopControlBuilder(graph, std::move(forest), quota).Build();
}
}  // namespace abcd::source
