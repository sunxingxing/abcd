#include "exception_topology.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = ExceptionControlError;
void Require(bool valid, Error error) { if (!valid) throw error; }
struct ControlScope {
  TryId region;
  CatchPart part;
  auto operator<=>(const ControlScope&) const = default;
};
using Scope = std::optional<ControlScope>;
struct RegionEntry { Scope parent; WorkBlockId block; TryId region; };
struct RegionInterval { std::uint32_t begin = 0; std::uint32_t end = 0; };
struct RegionVisit { TryId region; bool leave; };
Scope ScopeOf(const std::optional<CatchScope>& scope) {
  if (!scope) return {};
  return ControlScope{scope->region, scope->part};
}
class Projection {
 public:
  Projection(const ValueProgram& graph, const ExceptionPlan& regions, ControlQuota& quota)
      : graph_(graph), regions_(regions), quota_(quota) {}
  ExceptionTopology Build() {
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    const auto blocks = graph_.blocks.size(), regions = regions_.regions.size();
    // At most one exit unit per original normal edge and per wrapper.
    Require(blocks != 0 && blocks < kMaxId && regions <= (kMaxId - blocks) / 2 &&
        graph_.edges.size() <= kMaxId - blocks - 2 * regions, Error::kResourceLimit);
    const auto capacity = blocks + 2 * regions + graph_.edges.size();
    quota_.Size(result_.blocks, capacity);
    quota_.Size(result_.mapping.units, capacity);
    quota_.Size(result_.edges, graph_.edges.size() + regions);
    quota_.Size(result_.mapping.arcs, result_.edges.size());
    quota_.Size(result_.mapping.regions, regions);
    quota_.Size(result_.entries, 1 + 2 * regions + blocks);
    quota_.Size(result_.mapping.inactive_commits, blocks);
    quota_.Size(owners_, blocks);
    quota_.Size(exits_, regions);
    quota_.Size(entries_, regions);
    unit_count_ = static_cast<std::uint32_t>(blocks + regions);
    Index(); Exits();
    std::size_t root_count = 0;
    result_.entries[root_count++] = Target({0}, {});
    for (std::uint32_t b = 0; b < blocks; ++b) {
      quota_.Work();
      result_.mapping.units[b] = PhaseControlUnit{{b}};
      auto& projected = result_.blocks[b];
      projected.first_edge = {edge_count_};
      const auto& original = graph_.blocks[b];
      for (std::uint8_t e = 0; e < original.edge_count; ++e) {
        quota_.Work(); const EdgeId id{original.first_edge.value + e};
        if (std::holds_alternative<RaiseTransfer>(regions_.transfers[id.value])) continue;
        const auto& edge = graph_.edges[id.value];
        const auto target = Target(edge.target, owners_[b], id);
        Arc({b}, target, edge.kind, id);
      }
    }
    for (std::uint32_t r = 0; r < regions; ++r) {
      quota_.Work(); const TryId region{r}; const auto wrapper = Wrapper(region);
      const auto& original = regions_.regions[r];
      Require(original.binding.has_value(), Error::kRegion);
      result_.mapping.units[wrapper.value] = TryControlUnit{region};
      auto& planned = result_.mapping.regions[r];
      planned = {wrapper, Target(original.body, ControlScope{region, CatchPart::kProtected}),
          {original.binding->block.value}, exits_[r]};
      result_.blocks[wrapper.value].first_edge = {edge_count_};
      if (exits_[r]) {
        const auto target = Target(*exits_[r], ScopeOf(regions_.layout.regions[r].parent), region);
        Arc(wrapper, target, FlowEdgeKind::kTaken, {});
      }
      result_.entries[root_count++] = planned.body;
      result_.entries[root_count++] = planned.handler;
    }
    for (std::size_t u = blocks + regions; u < unit_count_; ++u) {
      quota_.Work(); result_.blocks[u].first_edge = {edge_count_};
    }
    for (const auto block : result_.mapping.inactive_commits) {
      quota_.Work(); result_.entries[root_count++] = {block.value};
    }
    result_.blocks.resize(unit_count_); result_.mapping.units.resize(unit_count_);
    result_.edges.resize(edge_count_); result_.mapping.arcs.resize(edge_count_);
    result_.entries.resize(root_count);
    return std::move(result_);
  }
 private:
  void Index() {
    const auto count = regions_.regions.size();
    quota_.Size(intervals_, count);
    std::vector<std::optional<TryId>> heads, siblings;
    std::vector<RegionVisit> visits;
    quota_.Size(heads, count + 1); quota_.Size(siblings, count); quota_.Size(visits, 2 * count);
    for (std::uint32_t r = 0; r < count; ++r) {
      quota_.Work(); const TryId region{r};
      const auto parent = ScopeOf(regions_.layout.regions[r].parent);
      const auto slot = parent ? parent->region.value : count;
      siblings[r] = heads[slot]; heads[slot] = region;
      entries_[r] = {parent, regions_.regions[r].body, region};
    }
    std::size_t pending = 0;
    for (auto root = heads[count]; root; root = siblings[root->value]) {
      quota_.Work(); visits[pending++] = {*root, false};
    }
    std::uint32_t stamp = 0;
    while (pending != 0) {
      quota_.Work(); const auto visit = visits[--pending];
      auto& interval = intervals_[visit.region.value];
      if (visit.leave) { interval.end = stamp; continue; }
      interval.begin = stamp++;
      Require(pending < visits.size(), Error::kResourceLimit);
      visits[pending++] = {visit.region, true};
      for (auto child = heads[visit.region.value]; child; child = siblings[child->value]) {
        quota_.Work(); Require(pending < visits.size(), Error::kResourceLimit);
        visits[pending++] = {*child, false};
      }
    }
    Require(stamp == count, Error::kRegion);
    std::sort(entries_.begin(), entries_.end(), [&](const auto& a, const auto& b) {
      quota_.Work(); return std::tie(a.parent, a.block) < std::tie(b.parent, b.block);
    });
    for (std::size_t i = 1; i < entries_.size(); ++i) {
      quota_.Work(); Require(entries_[i - 1].parent != entries_[i].parent ||
          entries_[i - 1].block != entries_[i].block, Error::kCrossEntry);
    }
    for (std::size_t b = 0; b < graph_.phases.size(); ++b) {
      quota_.Work(); owners_[b] = ScopeOf(regions_.layout.scopes[graph_.phases[b].instruction.value]);
    }
    // Catch executes outside any nested try beginning at the handler's first
    // instruction. PC/range membership alone gives the wrong lexical owner.
    for (std::uint32_t r = 0; r < count; ++r) {
      quota_.Work(); const auto& binding = regions_.regions[r].binding;
      Require(binding.has_value(), Error::kRegion);
      owners_[binding->block.value] = ControlScope{{r}, CatchPart::kHandler};
    }
    std::vector<std::uint32_t> incoming;
    quota_.Size(incoming, graph_.blocks.size());
    for (const auto& edge : graph_.edges) { quota_.Work(); ++incoming[edge.target.value]; }
    std::size_t inactive = 0;
    for (std::uint32_t b = 1; b < graph_.blocks.size(); ++b) {
      quota_.Work(); if (incoming[b] != 0) continue;
      const auto& phase = graph_.phases[b]; const auto& block = graph_.blocks[b];
      Require(phase.phase == ExceptionPhase::kCommit && phase.operation &&
          std::holds_alternative<ValueThrow>(graph_.ops[phase.operation->value].operation) &&
          block.op_count == 0 && block.edge_count == 0, Error::kInput);
      result_.mapping.inactive_commits[inactive++] = {b};
    }
    result_.mapping.inactive_commits.resize(inactive);
  }
  bool Inside(TryId region, WorkBlockId block) const {
    if (!owners_[block.value]) return false;
    const auto& outer = intervals_[region.value];
    const auto& inner = intervals_[owners_[block.value]->region.value];
    return outer.begin <= inner.begin && inner.begin < outer.end;
  }
  void Exit(TryId region, WorkBlockId target) {
    auto& exit = exits_[region.value];
    Require(!exit || *exit == target, Error::kMultipleExits); exit = target;
  }
  void Exits() {
    for (std::size_t e = 0; e < graph_.edges.size(); ++e) {
      quota_.Work();
      if (std::holds_alternative<RaiseTransfer>(regions_.transfers[e])) continue;
      const auto& edge = graph_.edges[e]; const auto owner = owners_[edge.source.value];
      if (owner && !Inside(owner->region, edge.target)) Exit(owner->region, edge.target);
    }
    // One child summary propagates once, not a walk over ancestors per edge.
    for (std::size_t i = regions_.layout.source_order.size(); i != 0; --i) {
      quota_.Work(); const auto r = regions_.layout.source_order[i - 1];
      const auto parent = regions_.layout.regions[r.value].parent;
      if (parent && exits_[r.value] && !Inside(parent->region, *exits_[r.value]))
        Exit(parent->region, *exits_[r.value]);
    }
  }
  ControlId Wrapper(TryId region) const {
    return {static_cast<std::uint32_t>(graph_.blocks.size() + region.value)};
  }
  ControlId Target(WorkBlockId block, Scope scope, std::optional<TryExitSource> source = {}) {
    if (owners_[block.value] == scope) return {block.value};
    const auto entry = std::lower_bound(entries_.begin(), entries_.end(), std::pair{scope, block},
        [&](const RegionEntry& item, const auto& key) {
          quota_.Work(); return std::pair{item.parent, item.block} < key;
        });
    if (entry != entries_.end() && entry->parent == scope && entry->block == block)
      return Wrapper(entry->region);
    Require(source && scope && exits_[scope->region.value] == block && !Inside(scope->region, block),
        Error::kCrossEntry);
    Require(unit_count_ < result_.mapping.units.size(), Error::kResourceLimit);
    const ControlId id{unit_count_++};
    result_.mapping.units[id.value] = TryExitUnit{scope->region, *source};
    return id;
  }
  void Arc(ControlId source, ControlId target, FlowEdgeKind kind, std::optional<EdgeId> original) {
    Require(edge_count_ < result_.edges.size(), Error::kResourceLimit);
    auto& block = result_.blocks[source.value];
    Require(block.edge_count < kMaxNormalSuccessors, Error::kInput);
    result_.edges[edge_count_] = {{source.value}, {target.value}, kind, block.edge_count++};
    result_.mapping.arcs[edge_count_++] = {original};
  }
  const ValueProgram& graph_;
  const ExceptionPlan& regions_;
  ControlQuota& quota_;
  ExceptionTopology result_;
  std::vector<Scope> owners_;
  std::vector<RegionInterval> intervals_;
  std::vector<RegionEntry> entries_;
  std::vector<std::optional<WorkBlockId>> exits_;
  std::uint32_t unit_count_ = 0;
  std::uint32_t edge_count_ = 0;
};
}  // namespace
ExceptionTopology ProjectExceptionControl(const ValueProgram& graph,
    const ExceptionPlan& regions, ControlQuota& quota) {
  quota.Admit(1, sizeof(Projection));
  return Projection(graph, regions, quota).Build();
}
}  // namespace abcd::source
