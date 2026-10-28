#include "exception_topology.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace abcd::source {
namespace {
using namespace analysis;
struct ControlScope {
  TryId region; CatchPart part;
  auto operator<=>(const ControlScope&) const = default;
};
using Scope = std::optional<ControlScope>;
Scope ScopeOf(std::optional<CatchScope> s) {
  if(s)return ControlScope{s->region,s->part};
  return {};
}
template<class T> void Require(const T& ok, ExceptionControlError error = ExceptionControlError::kStructure) {
  if (!ok) throw error;
}
struct Entry {
  Scope parent;
  ControlId body;
  TryId region;
};
// The ordinary wrapper algorithm consumes a sealed graph, not the implementation
// edges of its statements. Both builder and verifier rebuild this projection
// from original identities. Close outcome/exception ports remain in mapping.
class Enclosure {
 public:
  Enclosure(ExceptionTopology base, const ExceptionPlan& plan,
      const ValueProgram& graph, ControlQuota& quota)
      : base_(std::move(base)), plan_(plan), graph_(graph), q_(quota) {}
  ExceptionTopology Build() {
    const auto count = base_.blocks.size(), regions = plan_.regions.size();
    Require(count < UINT32_MAX && regions < UINT32_MAX-count && base_.edges.size()<UINT32_MAX,
        ExceptionControlError::kResourceLimit);
    // Sparse summary admission bounds nested propagation as well as storage.
    // This is a resource arena limit, never a successful semantic truncation.
    constexpr std::size_t kCompletionSlotsPerInput = 8;
    Require(count+regions<=UINT32_MAX-base_.edges.size(),ExceptionControlError::kResourceLimit);
    const auto input_size=count+regions+base_.edges.size();
    Require(input_size<=SIZE_MAX/kCompletionSlotsPerInput,ExceptionControlError::kResourceLimit);
    max_ports_=input_size*kCompletionSlotsPerInput;
    q_.Size(scopes_, count); q_.Size(parents_, regions); q_.Size(wrappers_, regions);
    q_.Size(phase_, graph_.blocks.size()); q_.Size(raw_, graph_.phases.size());
    q_.Size(exits_, regions); q_.Size(out_.ports, regions);q_.Size(normal_ports_,regions);
    q_.Size(out_.mapping.regions, regions); q_.Size(out_.entries, base_.entries.size()+2*regions);
    q_.Size(entries_, regions);
    std::vector<std::uint8_t> retained;q_.Size(retained,regions);
    for(std::uint32_t r=0;r<regions;++r){q_.Work();retained[r]=Ordinary({r});}
    auto projected_scopes=ProjectExceptionScopes(graph_,plan_,retained,q_);
    std::size_t entry_count = 0;
    for (const auto r : plan_.layout.source_order) {
      q_.Work();
      const auto p = plan_.layout.regions[r.value].parent;
      parents_[r.value] = p ? Resolve(ScopeOf(p)) : Scope{};
    }
    for (std::uint32_t n=0;n<count;++n) {
      q_.Work();
      if (const auto* phase=std::get_if<PhaseControlUnit>(&base_.mapping.units[n])) {
        phase_[phase->block.value]=ControlId{n};
        scopes_[n]=ScopeOf(projected_scopes[phase->block.value]);
      }
    }
    // Catch bindings have their own arm, even if a child try starts at the
    // same instruction. Resolve only through retained ordinary enclosures.
    for (const auto r : plan_.layout.source_order) {
      q_.Work();const auto& region=plan_.regions[r.value];
      if (region.binding && phase_[region.binding->block.value])
        scopes_[phase_[region.binding->block.value]->value]=Resolve(ControlScope{r,CatchPart::kHandler});
    }
    for (std::uint32_t b=0;b<graph_.phases.size();++b) {
      q_.Work();raw_[b]=graph_.phases[b].input;
    }
    // Original input identities are indexed once, not searched for each port.
    std::vector<std::optional<Scope>> input_scope;
    std::vector<std::optional<WorkBlockId>> input_phase;
    std::size_t raw_count=0;
    for (const auto& p:graph_.phases){q_.Work();raw_count=std::max(raw_count,std::size_t(p.input.value)+1);}
    q_.Size(input_scope,raw_count);q_.Size(input_phase,raw_count);
    for (std::uint32_t b=0;b<graph_.phases.size();++b) {
      q_.Work();input_phase[raw_[b].value]=WorkBlockId{b};input_scope[raw_[b].value]=ScopeOf(projected_scopes[b]);
    }
    for (std::uint32_t n=0;n<count;++n)if(const auto* close=std::get_if<ProtocolCloseUnit>(&base_.mapping.units[n])) {
      q_.Work();Require(close->entry.source.value<input_scope.size() && input_scope[close->entry.source.value]);
      scopes_[n]=*input_scope[close->entry.source.value];
    }
    next_=static_cast<std::uint32_t>(count);
    for (const auto r:plan_.layout.source_order) {
      q_.Work();if(!Ordinary(r))continue;
      wrappers_[r.value]=ControlId{next_++};
      const auto body=phase_[plan_.regions[r.value].body.value];
      Require(body && plan_.regions[r.value].binding);
      entries_[entry_count++]={parents_[r.value],*body,r};
    }
    entries_.resize(entry_count);
    std::sort(entries_.begin(),entries_.end(),[&](const auto& a,const auto& b){q_.Work();return std::tie(a.parent,a.body)<std::tie(b.parent,b.body);});
    for(std::size_t i=1;i<entries_.size();++i){q_.Work();Require(entries_[i-1].parent!=entries_[i].parent || entries_[i-1].body!=entries_[i].body,ExceptionControlError::kCrossEntry);}
    Index();
    for(const auto& edge:base_.edges) {
      q_.Work();const auto owner=scopes_[edge.source.value];
      if(owner && Leaves(*owner,{edge.target.value}))Exit(owner->region,{edge.target.value});
    }
    for(auto i=plan_.layout.source_order.size();i!=0;--i) {
      q_.Work();const auto r=plan_.layout.source_order[i-1];if(!Ordinary(r))continue;
      auto& exits=exits_[r.value];
      std::sort(exits.begin(),exits.end(),[&](auto a,auto b){q_.Work();return a<b;});
      exits.erase(std::unique(exits.begin(),exits.end()),exits.end());
      if(const auto p=parents_[r.value])for(auto e:exits){q_.Work();if(Leaves(*p,e))Exit(p->region,e);}
    }
    Require(base_.edges.size()<=UINT32_MAX-next_,ExceptionControlError::kResourceLimit);
    for(std::size_t r=0;r<exits_.size();++r){q_.Work();q_.Size(normal_ports_[r],exits_[r].size());}
    std::size_t capacity=next_+base_.edges.size();
    for(const auto& exits:exits_){q_.Work();Require(exits.size()<=UINT32_MAX-capacity,ExceptionControlError::kResourceLimit);capacity+=exits.size();}
    q_.Size(out_.blocks,capacity);q_.Size(out_.mapping.units,capacity);q_.Size(out_.owners,capacity);
    q_.Size(out_.edges,base_.edges.size());q_.Size(out_.mapping.arcs,base_.edges.size());
    std::size_t roots=0;
    for(const auto root:base_.entries){q_.Work();out_.entries[roots++]=Target(root,scopes_[root.value]);}
    // The file entry enters a wrapper if the try starts at the first phase.
    Require(!base_.entries.empty());out_.entries[0]=Target(base_.entries[0],{});
    for(std::uint32_t n=0;n<count;++n) {
      q_.Work();out_.mapping.units[n]=base_.mapping.units[n];
      out_.owners[n]=scopes_[n]?std::optional{scopes_[n]->region}:std::nullopt;
      auto& node=out_.blocks[n];node=base_.blocks[n];node.first_arc={arcs_};node.arc_count=0;
      const auto& block=base_.blocks[n];
      for(std::size_t a=0;a<block.arc_count;++a) {
        q_.Work();const auto index=block.first_arc.value+a;const auto& edge=base_.edges[index];
        const auto origin=base_.mapping.arcs[index].edge;
        std::optional<TryExitSource> source;
        if(origin)source=*origin;
        else if(const auto* close=std::get_if<ProtocolCloseUnit>(&base_.mapping.units[n])) {
          // A synthetic continuation is the completion of this ORIGINAL
          // entry-qualified native exit, not an implementation close edge.
          const auto phase=input_phase[close->entry.source.value];Require(phase);
          const auto& block=graph_.blocks[phase->value];
          for(std::uint32_t e=0;e<block.edge_count;++e){q_.Work();const EdgeId id{block.first_edge.value+e};if(graph_.edges[id.value].successor==close->entry.successor){Require(!source);source=id;}}
          Require(source);
        }
        const auto target=Target({edge.target.value},scopes_[n],source);
        auto& b=out_.blocks[n];Require(arcs_<out_.edges.size());
        out_.edges[arcs_]={{n},{target.value},edge.kind,b.arc_count++};out_.mapping.arcs[arcs_++]=base_.mapping.arcs[index];
      }
    }
    for(const auto r:plan_.layout.source_order) {
      q_.Work();if(!Ordinary(r))continue;
      const auto wrapper=*wrappers_[r.value];const auto& region=plan_.regions[r.value];
      const auto body=Target(*phase_[region.body.value],ControlScope{r,CatchPart::kProtected});
      const auto handler=phase_[region.binding->block.value];Require(handler);
      out_.mapping.units[wrapper.value]=TryControlUnit{r};
      out_.mapping.regions[r.value]=TryControlRegion{wrapper,body,*handler,{}};
      out_.owners[wrapper.value]=parents_[r.value]?std::optional{parents_[r.value]->region}:std::nullopt;
      q_.Size(out_.ports[r.value],exits_[r.value].size());
      for(std::size_t i=0;i<exits_[r.value].size();++i) {
        q_.Work();const auto e=exits_[r.value][i];const auto& unit=base_.mapping.units[e.value];
        const auto* phase=std::get_if<PhaseControlUnit>(&unit);Require(phase);
        out_.ports[r.value][i]={phase->block,{Target(e,parents_[r.value],r).value}};
      }
      // Loop/try composition sorts and indexes ports by ORIGINAL phase target.
      std::sort(out_.ports[r.value].begin(),out_.ports[r.value].end(),[&](const auto& a,const auto& b){q_.Work();return a.target<b.target;});
      out_.entries[roots++]=body;out_.entries[roots++]=*handler;
    }
    for(std::size_t n=count;n<next_;++n){q_.Work();out_.blocks[n].first_arc={arcs_};}
    out_.blocks.resize(next_);out_.mapping.units.resize(next_);out_.owners.resize(next_);
    out_.edges.resize(arcs_);out_.mapping.arcs.resize(arcs_);
    std::vector<bool> seen;q_.Size(seen,next_);std::size_t unique=0;
    for(std::size_t i=0;i<roots;++i){q_.Work();const auto r=out_.entries[i];Require(r.value<seen.size());if(!seen[r.value]){seen[r.value]=true;out_.entries[unique++]=r;}}
    out_.entries.resize(unique);
    out_.mapping.continuations=std::move(base_.mapping.continuations);
    out_.mapping.protocol_ports=std::move(base_.mapping.protocol_ports);
    out_.mapping.inactive_phases=std::move(base_.mapping.inactive_phases);
    // The shared ordinary try/loop composer receives a lexical layout with
    // only retained wrappers. Parent links skip certified protocol dispatch.
    std::vector<CatchRegion> regions_layout;
    std::vector<TryId> order;
    q_.Size(regions_layout,plan_.layout.regions.size());
    q_.Size(order,entries_.size());
    std::copy(plan_.layout.regions.begin(),plan_.layout.regions.end(),regions_layout.begin());
    std::size_t rcount=0;
    for(const auto r:plan_.layout.source_order){q_.Work();if(Ordinary(r)){order[rcount++]=r;if(parents_[r.value])regions_layout[r.value].parent=CatchScope{parents_[r.value]->region,parents_[r.value]->part};else regions_layout[r.value].parent.reset();}}
    out_.mapping.region_roles=std::move(base_.mapping.region_roles);
    ComposeExceptionLoops(out_,{order,regions_layout},graph_,q_);
    return std::move(out_);
  }
 private:
  bool Ordinary(TryId r) const {return base_.mapping.region_roles[r.value]==ExceptionRegionRole::kOrdinaryTry;}
  Scope Resolve(Scope scope) const {
    if(!scope || Ordinary(scope->region))return scope;
    return parents_[scope->region.value];
  }
  void Index() {
    q_.Size(begin_,plan_.regions.size());q_.Size(end_,plan_.regions.size());
    struct Visit{TryId region;bool leave;};
    std::vector<Visit> stack;std::vector<std::optional<TryId>> heads,siblings;
    q_.Size(stack,2*entries_.size());q_.Size(heads,plan_.regions.size()+1);q_.Size(siblings,plan_.regions.size());
    for(const auto& e:entries_){q_.Work();const auto p=e.parent?e.parent->region.value:plan_.regions.size();siblings[e.region.value]=heads[p];heads[p]=e.region;}
    std::size_t size=0,stamp=0;
    for(auto r=heads.back();r;r=siblings[r->value])stack[size++]={*r,false};
    while(size){q_.Work();const auto v=stack[--size];if(v.leave){end_[v.region.value]=stamp;continue;}begin_[v.region.value]=stamp++;stack[size++]={v.region,true};for(auto r=heads[v.region.value];r;r=siblings[r->value])stack[size++]={*r,false};}
  }
  bool Leaves(ControlScope scope,ControlId target) const {
    const auto owner=scopes_[target.value];
    const bool inside=owner && begin_[scope.region.value]<=begin_[owner->region.value] && begin_[owner->region.value]<end_[scope.region.value];
    return !inside || (scope.part==CatchPart::kHandler && phase_[plan_.regions[scope.region.value].body.value]==target);
  }
  void Exit(TryId r,ControlId target) {
    Require(port_count_<max_ports_,ExceptionControlError::kResourceLimit);++port_count_;
    auto& exits=exits_[r.value];
    if(exits.size()==exits.capacity()){const auto n=exits.empty()?1:exits.size()*2;q_.Admit(n,sizeof(ControlId));exits.reserve(n);}
    exits.push_back(target);
  }
  ControlId Target(ControlId node,Scope scope,std::optional<TryExitSource> source={}) {
    if(scopes_[node.value]==scope)return node;
    const auto e=std::lower_bound(entries_.begin(),entries_.end(),std::pair{scope,node},[&](const auto& a,const auto& b){q_.Work();return std::pair{a.parent,a.body}<b;});
    if(e!=entries_.end() && e->parent==scope && e->body==node)return *wrappers_[e->region.value];
    Require(source && scope && Leaves(*scope,node),ExceptionControlError::kCrossEntry);
    Require(next_<out_.mapping.units.size(),ExceptionControlError::kResourceLimit);
    const auto* phase=std::get_if<PhaseControlUnit>(&base_.mapping.units[node.value]);Require(phase);
    const auto& exits=exits_[scope->region.value];
    const auto at=std::lower_bound(exits.begin(),exits.end(),node,[&](auto a,auto b){q_.Work();return a<b;});
    Require(at!=exits.end() && *at==node);
    auto& port=normal_ports_[scope->region.value][at-exits.begin()][scope->part==CatchPart::kProtected?0:1];
    if(port)return *port;
    // One completion-keyed normal port, with EVERY incoming original arc
    // retained above. Edge-specific terminal copies would spuriously give
    // the native loop two different exits (exhaustion versus native break).
    // TryId denotes the region summary, not a fabricated original edge.
    const ControlId id{next_++};port=id;
    out_.mapping.units[id.value]=TryExitUnit{scope->region,scope->region,phase->block};out_.owners[id.value]=scope->region;return id;
  }
  ExceptionTopology base_,out_;const ExceptionPlan& plan_;const ValueProgram& graph_;ControlQuota& q_;
  std::vector<Scope> scopes_,parents_;
  std::vector<std::optional<ControlId>> wrappers_,phase_;
  std::vector<ExceptionNodeId> raw_;
  std::vector<std::vector<ControlId>> exits_;
  std::vector<std::vector<std::array<std::optional<ControlId>,2>>> normal_ports_;
  std::vector<Entry> entries_;
  std::vector<std::size_t> begin_,end_;
  std::size_t port_count_=0,max_ports_=0;
  std::uint32_t next_=0,arcs_=0;
};
}
ExceptionTopology ComposeOrdinaryEnclosures(ExceptionTopology base,const ExceptionPlan& plan,
    const ValueProgram& graph,ControlQuota& q) {
  q.Admit(1,sizeof(Enclosure));return Enclosure(std::move(base),plan,graph,q).Build();
}
}
