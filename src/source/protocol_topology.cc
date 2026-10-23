#include "exception_topology.h"
#include <algorithm>
#include <limits>
namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool b) {if(!b)throw ExceptionControlError::kStructure;}
struct EntryIndex {
  CompletionEdge edge;
  std::uint32_t unit;
  bool operator<(const EntryIndex& b) const {return edge<b.edge;}
};
}
ExceptionTopology ProjectProtocolControl(const ValueProgram& g, const ExceptionPlan& plan,
    const PhasedValueInput& input, const VerifiedProtocolRegionOwnership& ownership, ControlQuota& q) {
  const auto& summary=ownership.Summary();
  Require(summary.revision==g.revision && summary.cfg_revision==g.cfg_revision);
  ExceptionTopology t;
  std::vector<bool> members;
  std::vector<std::optional<ControlId>> raw, phase;
  std::vector<EntryIndex> entries;
  std::vector<std::optional<CloseIterationBoundary>> iterations;
  q.Size(iterations,ownership.Scan().candidates.size());
  q.Size(members,input.flow.nodes.size()); q.Size(raw,input.flow.nodes.size());
  q.Size(phase,g.blocks.size());q.Size(entries,summary.entries.size());
  q.Size(t.mapping.regions,plan.regions.size());q.Size(t.mapping.region_roles,plan.regions.size());
  for(std::size_t i=0;i<plan.regions.size();++i) {
    q.Work();t.mapping.region_roles[i]=plan.layout.regions[i].inactive?
        ExceptionRegionRole::kInactive:ExceptionRegionRole::kOrdinaryTry;
  }
  std::size_t ports=0;
  for(const auto& region:summary.regions) {
    q.Work();Require(region.boundary && region.outcomes && region.protocol.value<iterations.size());
    auto& iteration=iterations[region.protocol.value];
    if(iteration)Require(*iteration==region.iteration);else iteration=region.iteration;
    Require(region.boundary->ports.size()<=UINT32_MAX-ports);
    ports+=region.boundary->ports.size();
    for(const auto& member:region.members) {q.Work();Require(member.node.value<members.size());members[member.node.value]=true;}
    for(const auto id:region.boundary->regions) {
      q.Work();Require(id.value<t.mapping.region_roles.size());
      t.mapping.region_roles[id.value]=ExceptionRegionRole::kProtocolOwned;
    }
  }
  const auto& forwarding=ownership.Forwarding();
  if(!forwarding.regions.empty()) {
    std::vector<std::uint8_t> owned,retained,entered,close;q.Size(owned,plan.regions.size());q.Size(retained,plan.regions.size());q.Size(entered,plan.regions.size());q.Size(close,g.blocks.size());
    for(std::size_t r=0;r<owned.size();++r){q.Work();owned[r]=entered[r]=t.mapping.region_roles[r]==ExceptionRegionRole::kProtocolOwned;}
    for(std::size_t b=0;b<close.size();++b){q.Work();close[b]=members[g.phases[b].input.value];}
    for(const auto& region:summary.regions)for(const auto id:region.boundary->enclosures){q.Work();owned[id.value]=true;retained[id.value]=true;}
    std::vector<TryId> bodies;
    {std::size_t count=0;for(const auto& r:summary.regions){q.Work();for(const auto& range:r.boundary->protected_ranges)count+=range.role==ProtectedRangeRole::kLoopBody;}
     q.Size(bodies,count);count=0;for(const auto& r:summary.regions)for(const auto& range:r.boundary->protected_ranges)if(range.role==ProtectedRangeRole::kLoopBody)bodies[count++]=range.region;}
    const auto verified=AnalyzeProtocolForwarding(g,plan,owned,close,q.Remaining(),retained,entered,bodies);
    if(const auto* f=std::get_if<ForInFailure>(&verified))throw f->kind==ForInError::kResourceLimit?ExceptionControlError::kResourceLimit:f->kind==ForInError::kWorkLimit?ExceptionControlError::kWorkLimit:ExceptionControlError::kStructure;
    const auto& actual=std::get<ProtocolForwarding>(verified);q.Admit(actual.allocated_bytes,1);q.Work(actual.work);
    q.Work(actual.regions.size()+actual.blocks.size()+actual.operations.size()+actual.exits.size());
    Require(actual==forwarding);
  }
  for(std::size_t r=0;r<forwarding.regions.size();++r){q.Work();if(forwarding.regions[r]){Require(t.mapping.region_roles[r]==ExceptionRegionRole::kOrdinaryTry);t.mapping.region_roles[r]=ExceptionRegionRole::kForwardedTry;}}
  // Retain ordinary enclosures around sealed statements. Only independently
  // proved transparent routes are erased; effectful user catches stay ordinary.
  bool ordinary=false;
  for(const auto role:t.mapping.region_roles) {q.Work();ordinary|=role==ExceptionRegionRole::kOrdinaryTry;}
  Require(g.blocks.size()<=UINT32_MAX-summary.entries.size());
  q.Size(t.mapping.units,g.blocks.size()+summary.entries.size());
  q.Size(t.mapping.protocol_ports,ports);
  std::size_t count=0;
  for(std::uint32_t b=0;b<g.blocks.size();++b) {
    q.Work();const auto& site=g.phases[b];Require(site.input.value<members.size());
    if(members[site.input.value] || (!forwarding.blocks.empty() && forwarding.blocks[b]) || site.reachability==PhaseReachability::kUnreachable)continue;
    const ControlId id{static_cast<std::uint32_t>(count++)};
    Require(!raw[site.input.value]);raw[site.input.value]=id;phase[b]=id;
    t.mapping.units[id.value]=PhaseControlUnit{{b}};
  }
  std::size_t port=0;
  std::uint32_t local=0;
  std::vector<CloseOutcomeLeaving> ordered_ports;
  std::optional<OpId> previous_read;
  for(std::uint32_t i=0;i<summary.entries.size();++i) {
    q.Work();const auto& entry=summary.entries[i];
    const auto region=std::lower_bound(summary.regions.begin(),summary.regions.end(),entry.read,
        [&](const auto& a,OpId read){q.Work();return a.read<read;});
    // Regions are original OpId ordered by the certificate producer.
    Require(region!=summary.regions.end() && region->read==entry.read && region->protocol==entry.protocol);
    const ControlId id{static_cast<std::uint32_t>(count++)};
    ProtocolCloseUnit unit{};unit.protocol=entry.protocol;unit.entry=entry.source;unit.pending=entry.pending;
    Require(!region->boundary->regions.empty());
    unit.first_port=static_cast<std::uint32_t>(port);
    // Relation entry numbers are local to each return-read occurrence.
    if(previous_read!=entry.read) {
      // Each port table is indexed once, never scanned once per close entry.
      // Retain the conservative previous allocation charge across resize.
      q.Size(ordered_ports,region->boundary->ports.size());
      std::copy(region->boundary->ports.begin(),region->boundary->ports.end(),ordered_ports.begin());
      std::sort(ordered_ports.begin(),ordered_ports.end(),[&](const auto& a,const auto& b){q.Work();return a.entry<b.entry;});
      previous_read=entry.read;local=0;
    } else ++local;
    auto p=std::lower_bound(ordered_ports.begin(),ordered_ports.end(),local,
        [&](const auto& a,std::uint32_t b){q.Work();return a.entry<b;});
    for(;p!=ordered_ports.end() && p->entry==local;++p) {
      q.Work();Require(port<t.mapping.protocol_ports.size());t.mapping.protocol_ports[port++]=*p;++unit.port_count;
    }
    Require(unit.port_count!=0);t.mapping.units[id.value]=unit;entries[i]={entry.source,id.value};
  }
  // Each relation exit occurs once per entry; no duplicated storage is charged
  // by the entry table. Inputs remain borrowed under their owner's live grant.
  Require(port==ports);t.mapping.units.resize(count);
  std::sort(entries.begin(),entries.end(),[&](const auto& a,const auto& b){q.Work();return a<b;});
  for(std::size_t i=1;i<entries.size();++i){q.Work();Require(entries[i-1].edge!=entries[i].edge);}
  q.Size(t.blocks,count);q.Size(t.owners,count);q.Size(t.entries,count);
  q.Size(t.edges,g.edges.size()+summary.entries.size());q.Size(t.mapping.arcs,t.edges.size());
  std::vector<std::uint32_t> incoming;q.Size(incoming,count);
  std::size_t edge_count=0;
  const auto add=[&](ControlId from,ControlId to,FlowEdgeKind kind,std::optional<EdgeId> origin) {
    auto& b=t.blocks[from.value];Require(b.arc_count<kMaxNormalSuccessors && edge_count<t.edges.size());
    t.edges[edge_count]={{from.value},{to.value},kind,b.arc_count++};t.mapping.arcs[edge_count++]={origin};++incoming[to.value];
  };
  for(std::uint32_t n=0;n<count;++n) {
    q.Work();auto& node=t.blocks[n];node.first_arc={static_cast<std::uint32_t>(edge_count)};
    if(const auto* block=std::get_if<PhaseControlUnit>(&t.mapping.units[n])) {
      node.termination=BlockTermination(g,g.blocks[block->block.value]);
      const auto& b=g.blocks[block->block.value];
      const auto op=g.phases[block->block.value].operation;
      if(op && (std::holds_alternative<ValueReturn>(g.ops[op->value].operation) ||
          std::holds_alternative<ValueThrow>(g.ops[op->value].operation)))node.termination=ControlTermination::kAbrupt;
      for(std::uint8_t e=0;e<b.edge_count;++e) {
        q.Work();const EdgeId id{b.first_edge.value+e};const auto& original=g.edges[id.value];
        if(std::holds_alternative<RaiseTransfer>(plan.transfers[id.value]))continue;
        const CompletionEdge source{g.phases[original.source.value].input,original.successor};
        const auto found=std::lower_bound(entries.begin(),entries.end(),EntryIndex{source,0},
            [&](const auto& a,const auto& b){q.Work();return a<b;});
        if(found!=entries.end() && found->edge==source)add({n},{found->unit},original.kind,id);
        else {Require(phase[original.target.value].has_value());add({n},*phase[original.target.value],original.kind,id);}
      }
    } else {
      const auto& unit=std::get<ProtocolCloseUnit>(t.mapping.units[n]);node.statement=true;
      switch(unit.pending.kind) {
        case PendingCompletionKind::kReturn:case PendingCompletionKind::kThrow:
          node.termination=ControlTermination::kAbrupt;break;
        case PendingCompletionKind::kNormal:case PendingCompletionKind::kBreak:case PendingCompletionKind::kContinue: {
          const auto* target=std::get_if<ExceptionNodeId>(&unit.pending.target);
          Require(target && target->value<raw.size() && raw[target->value]);
          auto destination=*target;
          if(unit.pending.kind==PendingCompletionKind::kBreak) {
            // Native break and exhaustion share the SAME source continuation.
            // Keep its original empty corridor rather than jumping past it
            // and creating two lexical loop exits after block splitting.
            Require(unit.protocol.value<iterations.size() && iterations[unit.protocol.value]);
            const auto& iteration=*iterations[unit.protocol.value];
            if(destination==iteration.exit) {
              const auto exhausted=iteration.exhausted;
              Require(exhausted.source.value<input.flow.nodes.size() &&
                  exhausted.successor<input.flow.nodes[exhausted.source.value].successor_count);
              destination=input.flow.nodes[exhausted.source.value].successors[exhausted.successor].target;
            }
          }
          Require(destination.value<raw.size() && raw[destination.value]);
          add({n},*raw[destination.value],FlowEdgeKind::kFallthrough,{});break;
        }
      }
    }
  }
  t.edges.resize(edge_count);t.mapping.arcs.resize(edge_count);
  std::size_t roots=0;Require(!phase.empty() && phase[0]);
  t.entries[roots++]=*phase[0];
  for(std::uint32_t n=0;n<count;++n) {
    q.Work();if(incoming[n]==0 && n!=phase[0]->value)t.entries[roots++]={n};
  }
  t.entries.resize(roots);
  return ordinary?ComposeOrdinaryEnclosures(std::move(t),plan,g,q):std::move(t);
}
}
