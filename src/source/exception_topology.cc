#include "exception_topology.h"

#include <algorithm>
#include <bit>
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
  Projection(const ValueProgram& graph, const ExceptionPlan& regions, ControlQuota& quota,
      std::span<const std::uint8_t> retained = {})
      : graph_(graph), regions_(regions), quota_(quota), retained_(retained) {}
  std::vector<std::optional<CatchScope>> Scopes() {
    quota_.Size(owners_,graph_.blocks.size());
    quota_.Size(result_.mapping.inactive_phases,graph_.blocks.size());
    std::size_t count=0;
    for(const auto r:regions_.layout.source_order){quota_.Work();count+=Active(r.value);}
    quota_.Size(entries_,count);
    Index(); Forwarders();
    std::vector<std::optional<CatchScope>> result;quota_.Size(result,owners_.size());
    for(std::size_t b=0;b<owners_.size();++b){quota_.Work();if(owners_[b])result[b]=CatchScope{owners_[b]->region,owners_[b]->part};}
    return result;
  }
  ExceptionTopology Build() {
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    const auto blocks = graph_.blocks.size(), regions = regions_.regions.size();
    Require(blocks != 0 && blocks < kMaxId && regions < kMaxId - blocks, Error::kResourceLimit);
    quota_.Size(result_.mapping.regions, regions);
    quota_.Size(result_.entries, 1 + 2 * regions + blocks);
    quota_.Size(result_.mapping.inactive_phases, blocks);
    quota_.Size(owners_, blocks);
    quota_.Size(exits_, regions);
    quota_.Size(result_.ports, regions);
    quota_.Size(entries_, regions_.layout.source_order.size());
    quota_.Size(wrappers_, regions);
    unit_count_ = static_cast<std::uint32_t>(blocks);
    for (std::uint32_t r = 0; r < regions; ++r) {
      quota_.Work(); if (regions_.layout.regions[r].inactive) continue;
      wrappers_[r] = ControlId{unit_count_++};
    }
    wrapper_end_ = unit_count_;
    PreservingCompletions(); Index(); Forwarders(); Exits();
    std::size_t capacity = blocks + regions;
    const auto add = [&](std::size_t count) {
      Require(count <= kMaxId - capacity, Error::kResourceLimit); capacity += count;
    };
    add(graph_.edges.size());
    for (const auto& exits : exits_) { quota_.Work(); add(exits.size()); }
    quota_.Size(result_.blocks, capacity); quota_.Size(result_.mapping.units, capacity);
    quota_.Size(result_.owners, capacity);
    quota_.Size(result_.edges, graph_.edges.size()); quota_.Size(result_.mapping.arcs, graph_.edges.size());
    std::size_t root_count = 0;
    result_.entries[root_count++] = Target({0}, {});
    for (std::uint32_t b = 0; b < blocks; ++b) {
      quota_.Work();
      result_.mapping.units[b] = PhaseControlUnit{{b}};
      if (owners_[b]) result_.owners[b] = owners_[b]->region;
      auto& projected = result_.blocks[b];
      projected.first_arc = {edge_count_};
      const auto& original = graph_.blocks[b];
      // A normal try-exit port is not a source return. Preserve terminal kind
      // across projection so early-exit structuring cannot skip wrapper.next.
      projected.termination = BlockTermination(graph_, original);
      const auto operation = graph_.phases[b].operation;
      // Return/throw may already be emitted in Before; its empty Commit is
      // still the end of that abrupt corridor, unlike an ordinary exit port.
      if (operation && (std::holds_alternative<ValueReturn>(graph_.ops[operation->value].operation) ||
                        std::holds_alternative<ValueThrow>(graph_.ops[operation->value].operation)))
        projected.termination = ControlTermination::kAbrupt;
      if (operation && !preserving_.empty() && preserving_[operation->value])
        projected.completion = ControlCompletion::kPreserving;
      for (std::uint8_t e = 0; e < original.edge_count; ++e) {
        quota_.Work(); const EdgeId id{original.first_edge.value + e};
        if (std::holds_alternative<RaiseTransfer>(regions_.transfers[id.value])) continue;
        const auto& edge = graph_.edges[id.value];
        const auto target = Target(edge.target, owners_[b], id);
        Arc({b}, target, edge.kind, id);
      }
    }
    for (std::uint32_t r = 0; r < regions; ++r) {
      quota_.Work(); if (regions_.layout.regions[r].inactive) continue;
      const TryId region{r}; const auto wrapper = Wrapper(region);
      const auto& original = regions_.regions[r];
      Require(original.binding.has_value(), Error::kRegion);
      result_.mapping.units[wrapper.value] = TryControlUnit{region};
      if (const auto parent = regions_.layout.regions[r].parent) result_.owners[wrapper.value] = parent->region;
      auto& planned = result_.mapping.regions[r].emplace();
      planned = {wrapper, Target(original.body, ControlScope{region, CatchPart::kProtected}),
          {original.binding->block.value}, {}};
      result_.blocks[wrapper.value].first_arc = {edge_count_};
      // No exit port: every path through body and handler returns, throws or
      // diverges, so the wrapper never falls through to an enclosing arm's
      // continuation. Layout keeps treating it as ordinary (kExitless).
      if (exits_[r].empty()) result_.blocks[wrapper.value].termination = ControlTermination::kExitless;
      quota_.Size(result_.ports[r], exits_[r].size());
      for (std::size_t e = 0; e < exits_[r].size(); ++e) {
        quota_.Work(); const auto exit = exits_[r][e];
        const auto target = Target(exit, ScopeOf(regions_.layout.regions[r].parent), region);
        result_.ports[r][e] = {exit, {target.value}};
      }
      result_.entries[root_count++] = planned.body;
      result_.entries[root_count++] = planned.handler;
    }
    for (std::size_t u = wrapper_end_; u < unit_count_; ++u) {
      quota_.Work(); result_.blocks[u].first_arc = {edge_count_};
    }
    for (const auto block : result_.mapping.inactive_phases) {
      quota_.Work(); result_.entries[root_count++] = {block.value};
    }
    result_.blocks.resize(unit_count_); result_.mapping.units.resize(unit_count_);
    result_.owners.resize(unit_count_);
    result_.edges.resize(edge_count_); result_.mapping.arcs.resize(edge_count_);
    result_.entries.resize(root_count);
    ComposeExceptionLoops(result_, {regions_.layout.source_order,regions_.layout.regions}, graph_, quota_);
    return std::move(result_);
  }
 private:
  void PreservingCompletions() {
    // OH7 FunctionBuilder::HandleCompletion fixes the caller payload's RETURN
    // after ControlFlowChangeReturn. Its normalized owner identifies that exact
    // terminal; loop shape cannot distinguish it from a cleanup override.
    // Preserve both Before/Commit phases, the Return and every original edge.
    // O(O log(Y+1)+Y log(Y+1)); no per-loop or per-suspension graph walks.
    std::size_t count = 0;
    for (const auto& op : graph_.ops) {
      quota_.Work(); const auto* yield = std::get_if<ValueYield>(&op.operation);
      if (yield && yield->origin.return_route && yield->origin.return_route->completion) ++count;
    }
    if (!count) return;
    std::vector<abc::CodeOffset> completions;
    quota_.Size(completions, count); count = 0;
    for (const auto& op : graph_.ops) {
      quota_.Work(); const auto* yield = std::get_if<ValueYield>(&op.operation);
      if (yield && yield->origin.return_route && yield->origin.return_route->completion)
        completions[count++] = *yield->origin.return_route->completion;
    }
    std::sort(completions.begin(), completions.end(), [&](auto a, auto b) { quota_.Work(); return a < b; });
    completions.erase(std::unique(completions.begin(), completions.end()), completions.end());
    quota_.Size(preserving_, graph_.ops.size());
    std::size_t found = 0;
    for (std::uint32_t i = 0; i < graph_.ops.size(); ++i) {
      quota_.Work(); const auto& op = graph_.ops[i];
      if (!std::holds_alternative<ValueReturn>(op.operation)) continue;
      Require(op.origin.value < graph_.origins.size(), Error::kInput);
      const auto& origin = graph_.origins[op.origin.value];
      const auto pc = origin.original.value_or(origin.pc);
      const auto at = std::lower_bound(completions.begin(), completions.end(), pc,
          [&](auto a, auto b) { quota_.Work(); return a < b; });
      if (at != completions.end() && *at == pc) { preserving_[i] = true; ++found; }
    }
    Require(found == completions.size(), Error::kInput);
  }
  bool Active(std::size_t r) const {
    return retained_.empty() ? !regions_.layout.regions[r].inactive : retained_[r]!=0;
  }
  Scope Resolve(Scope s) const {
    return !s || Active(s->region.value) ? s : parents_[s->region.value];
  }
  Scope Parent(std::size_t r) const {
    return retained_.empty()?ScopeOf(regions_.layout.regions[r].parent):parents_[r];
  }
  void Index() {
    if(!retained_.empty()) {
      Require(retained_.size()==regions_.regions.size(),Error::kInput);
      quota_.Size(parents_,regions_.regions.size());
      for(const auto r:regions_.layout.source_order){quota_.Work();parents_[r.value]=Resolve(ScopeOf(regions_.layout.regions[r.value].parent));}
    }
    const auto count = regions_.regions.size();
    quota_.Size(intervals_, count);
    std::vector<std::optional<TryId>> heads, siblings;
    std::vector<RegionVisit> visits;
    quota_.Size(heads, count + 1); quota_.Size(siblings, count); quota_.Size(visits, 2 * count);
    std::size_t entry = 0;
    for (std::uint32_t r = 0; r < count; ++r) {
      quota_.Work(); if (!Active(r)) continue;
      const TryId region{r};
      const auto parent = Parent(r);
      const auto slot = parent ? parent->region.value : count;
      siblings[r] = heads[slot]; heads[slot] = region;
      entries_[entry++] = {parent, regions_.regions[r].body, region};
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
    Require(stamp == entries_.size(), Error::kRegion);
    std::sort(entries_.begin(), entries_.end(), [&](const auto& a, const auto& b) {
      quota_.Work(); return std::tie(a.parent, a.block) < std::tie(b.parent, b.block);
    });
    for (std::size_t i = 1; i < entries_.size(); ++i) {
      quota_.Work(); Require(entries_[i - 1].parent != entries_[i].parent ||
          entries_[i - 1].block != entries_[i].block, Error::kCrossEntry);
    }
    for (std::size_t b = 0; b < graph_.phases.size(); ++b) {
      quota_.Work(); owners_[b] = Resolve(ScopeOf(regions_.layout.scopes[graph_.phases[b].instruction.value]));
    }
    // Catch executes outside any nested try beginning at the handler's first
    // instruction. PC/range membership alone gives the wrong lexical owner.
    for (const auto id : regions_.layout.source_order) {
      quota_.Work(); const auto r = id.value; if(!Active(r))continue; const auto& binding = regions_.regions[r].binding;
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
      Require(phase.reachability == PhaseReachability::kUnreachable && block.edge_count == 0 &&
          (block.op_count == 0 || (block.op_count == 1 &&
              graph_.ops[block.first_op.value].life == ValueLife::kRemoved)), Error::kInput);
      result_.mapping.inactive_phases[inactive++] = {b};
    }
    result_.mapping.inactive_phases.resize(inactive);
  }
  bool Inside(TryId region, WorkBlockId block) const {
    if (!owners_[block.value]) return false;
    const auto& outer = intervals_[region.value];
    const auto& inner = intervals_[owners_[block.value]->region.value];
    return outer.begin <= inner.begin && inner.begin < outer.end;
  }
  void Forwarders() {
    struct Forwarder {
      std::uint32_t pending = 0;
      Scope incoming;
      bool candidate = false, seen = false, conflict = false;
    };
    std::vector<Forwarder> nodes;
    std::vector<WorkBlockId> queue;
    quota_.Size(nodes, graph_.blocks.size());
    quota_.Size(queue, graph_.blocks.size());
    // A pure corridor may leave several nested protected scopes at once.
    // Select the child scope just inside its original owner, not necessarily
    // the source's immediate parent. Binary lifting avoids an ancestor walk
    // per edge on deeply nested compiler catch/rethrow wrappers.
    const auto count = regions_.regions.size();
    const auto levels = std::bit_width(count);
    std::vector<std::size_t> depth;
    std::vector<Scope> ancestors;
    quota_.Size(depth, count);
    quota_.Size(ancestors, count * levels);
    for (const auto id : regions_.layout.source_order) {
      quota_.Work();if(!Active(id.value))continue;
      const auto parent = Parent(id.value);
      depth[id.value] = parent ? depth[parent->region.value] + 1 : 1;
      ancestors[id.value * levels] = parent;
      for (std::size_t level = 1; level < levels; ++level) {
        quota_.Work();
        const auto previous = ancestors[id.value * levels + level - 1];
        if (previous) ancestors[id.value * levels + level] = ancestors[previous->region.value * levels + level - 1];
      }
    }
    const auto entered_scope = [&](Scope source, Scope target) -> Scope {
      if (!source) return {};
      const auto target_depth = target ? depth[target->region.value] : 0;
      if (depth[source->region.value] <= target_depth) return {};
      auto distance = depth[source->region.value] - target_depth - 1;
      for (std::size_t level = 0; distance; ++level, distance >>= 1) {
        quota_.Work();
        if (distance & 1U) source = ancestors[source->region.value * levels + level];
      }
      return ancestors[source->region.value * levels] == target ? source : Scope{};
    };
    for (std::size_t b = 0; b < nodes.size(); ++b) {
      quota_.Work();
      const auto& phase = graph_.phases[b];
      if (phase.phase == ExceptionPhase::kCatch || !phase.operation) continue;
      const auto& op = graph_.ops[phase.operation->value];
      // Pure temporary transport can belong to the arm that uniquely enters
      // it: compiler finally initializes its pending sentinel outside the
      // protected PC range before joining the catch arm. Keep original SSA,
      // edge copies and semantic scopes; never absorb a throwing/effectful op.
      const bool transport = (std::holds_alternative<ValueCopy>(op.operation) && op.result &&
          graph_.values[op.result->value].domain.Contains(VmValueKind::kHole)) ||
          std::holds_alternative<ValueHole>(op.operation);
      const bool jump = std::holds_alternative<ValueJump>(op.operation) && op.use_count == 0 && !op.result;
      nodes[b].candidate = (jump || transport) && op.normal == LanguageEffect::kNone &&
          op.before_throw == LanguageEffect::kNone &&
          op.exceptional == ExceptionalCompletion::kImpossible &&
          graph_.blocks[b].edge_count == 1;
    }
    nodes.front().candidate = false;
    for (const auto& entry : entries_) {
      quota_.Work(); nodes[entry.block.value].candidate = false;
    }
    for (const auto& edge : graph_.edges) {
      quota_.Work();
      if (nodes[edge.target.value].candidate) ++nodes[edge.target.value].pending;
    }
    std::size_t first = 0, last = 0;
    for (std::uint32_t b = 0; b < nodes.size(); ++b) {
      quota_.Work();
      if (!nodes[b].candidate || nodes[b].pending == 0) queue[last++] = {b};
    }
    // Kahn propagation visits every edge once. Shared targets require unanimous
    // owners; unentered cycles remain unchanged for ordinary cycle rejection.
    // Retain original blocks/edges so phi copies and jump PC anchors still emit.
    while (first != last) {
      quota_.Work(); const auto source = queue[first++];
      const auto& block = graph_.blocks[source.value];
      for (std::uint32_t e = 0; e < block.edge_count; ++e) {
        quota_.Work();
        const auto target = graph_.edges[block.first_edge.value + e].target;
        auto& next = nodes[target.value];
        if (!next.candidate) continue;
        const auto promoted = entered_scope(owners_[source.value], owners_[target.value]);
        const auto scope = promoted ? promoted : owners_[target.value];
        if (!next.seen) { next.incoming = scope; next.seen = true; }
        else if (next.incoming != scope) next.conflict = true;
        Require(next.pending != 0, Error::kInput);
        if (--next.pending != 0) continue;
        if (!next.conflict) owners_[target.value] = next.incoming;
        Require(last < queue.size(), Error::kInput);
        queue[last++] = target;
      }
    }
  }
  void Exit(TryId region, WorkBlockId target) {
    auto& exits = exits_[region.value];
    if (exits.size() == exits.capacity()) {
      Require(exits.size() <= (std::numeric_limits<std::size_t>::max() - 1) / 2, Error::kResourceLimit);
      const auto capacity = exits.empty() ? 1 : exits.size() * 2;
      quota_.Admit(capacity, sizeof(WorkBlockId)); exits.reserve(capacity);
    }
    exits.push_back(target);
  }
  bool Leaves(ControlScope scope, WorkBlockId target) const {
    // A handler jumping to its protected entry closes this catch instance and
    // re-enters the try wrapper on the next loop iteration. A protected-body
    // backedge to the same PC instead stays inside the existing try.
    return !Inside(scope.region, target) ||
        (scope.part == CatchPart::kHandler && regions_.regions[scope.region.value].body == target);
  }
  void Exits() {
    for (std::size_t e = 0; e < graph_.edges.size(); ++e) {
      quota_.Work();
      if (std::holds_alternative<RaiseTransfer>(regions_.transfers[e])) continue;
      const auto& edge = graph_.edges[e]; const auto owner = owners_[edge.source.value];
      if (owner && Leaves(*owner, edge.target)) Exit(owner->region, edge.target);
    }
    // One child summary propagates once, not a walk over ancestors per edge.
    for (std::size_t i = regions_.layout.source_order.size(); i != 0; --i) {
      quota_.Work(); const auto r = regions_.layout.source_order[i - 1];
      auto& exits = exits_[r.value];
      std::sort(exits.begin(), exits.end(), [&](auto a, auto b) { quota_.Work(); return a < b; });
      exits.erase(std::unique(exits.begin(), exits.end()), exits.end());
      const auto parent = regions_.layout.regions[r.value].parent;
      if (parent) for (const auto exit : exits) {
        quota_.Work(); if (Leaves(*ScopeOf(parent), exit)) Exit(parent->region, exit);
      }
    }
  }
  ControlId Wrapper(TryId region) const {
    Require(region.value < wrappers_.size() && wrappers_[region.value].has_value(), Error::kRegion);
    return *wrappers_[region.value];
  }
  ControlId Target(WorkBlockId block, Scope scope, std::optional<TryExitSource> source = {}) {
    if (owners_[block.value] == scope) return {block.value};
    const auto entry = std::lower_bound(entries_.begin(), entries_.end(), std::pair{scope, block},
        [&](const RegionEntry& item, const auto& key) {
          quota_.Work(); return std::pair{item.parent, item.block} < key;
        });
    if (entry != entries_.end() && entry->parent == scope && entry->block == block)
      return Wrapper(entry->region);
    Require(source && scope && Leaves(*scope, block),
        Error::kCrossEntry);
    Require(unit_count_ < result_.mapping.units.size(), Error::kResourceLimit);
    const ControlId id{unit_count_++};
    result_.mapping.units[id.value] = TryExitUnit{scope->region, *source, block};
    result_.owners[id.value] = scope->region;
    return id;
  }
  void Arc(ControlId source, ControlId target, FlowEdgeKind kind, std::optional<EdgeId> original) {
    Require(edge_count_ < result_.edges.size(), Error::kResourceLimit);
    auto& block = result_.blocks[source.value];
    Require(block.arc_count < kMaxNormalSuccessors, Error::kInput);
    result_.edges[edge_count_] = {{source.value}, {target.value}, kind, block.arc_count++};
    result_.mapping.arcs[edge_count_++] = {original};
  }
  const ValueProgram& graph_;
  const ExceptionPlan& regions_;
  ControlQuota& quota_;
  ExceptionTopology result_;
  std::vector<bool> preserving_;
  std::vector<Scope> owners_,parents_;
  std::span<const std::uint8_t> retained_;
  std::vector<std::optional<ControlId>> wrappers_;
  std::uint32_t wrapper_end_ = 0;
  std::vector<RegionInterval> intervals_;
  std::vector<RegionEntry> entries_;
  std::vector<std::vector<WorkBlockId>> exits_;
  std::uint32_t unit_count_ = 0;
  std::uint32_t edge_count_ = 0;
};
}  // namespace
std::vector<std::optional<CatchScope>> ProjectExceptionScopes(const ValueProgram& graph,
    const ExceptionPlan& regions,std::span<const std::uint8_t> retained,ControlQuota& quota) {
  quota.Admit(1,sizeof(Projection));return Projection(graph,regions,quota,retained).Scopes();
}
ExceptionTopology ProjectExceptionControl(const ValueProgram& graph,
    const ExceptionPlan& regions, ControlQuota& quota) {
  quota.Admit(1, sizeof(Projection));
  return Projection(graph, regions, quota).Build();
}
}  // namespace abcd::source
