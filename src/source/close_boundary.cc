#include "close_boundary.h"
#include "close_loop_body.h"
#include "abcd/analysis/raw_event_graph.h"
#include <algorithm>
#include <bit>
#include <set>
#include <tuple>

namespace abcd::source {
namespace {
using namespace analysis;
struct WorkLess {
  RawEventArena* arena;
  template<class T> bool operator()(const T& a,const T& b) const {arena->Step();return a<b;}
};
class Boundary {
 public:
  Boundary(const abc::MethodBody& body,abc::ProfileId profile,const ExceptionFlow& flow,std::uint64_t frame,
      const ExceptionPlan& plan,const IteratorProtocolScan& scan,const DerivedClose& close,
      const CloseCompletionRegion& region,SemanticBudget budget,bool reverse,const CloseBodyIndex* entry_dominators)
      :flow_(flow),plan_(plan),scan_(scan),c_(close),r_(region),arena_(budget),raw_(body,arena_),
       member_(arena_.Resource()),reachable_(arena_.Resource()),flow_raw_(arena_.Resource()),raw_flow_(arena_.Resource()),
       private_(WorkLess{&arena_},arena_.Resource()),reverse_(reverse),entry_dominators_(entry_dominators) {
    const auto n=std::uint64_t(flow.nodes.size())+plan.regions.size()+1;
    Require(n<=UINT32_MAX && plan.layout.regions.size()==plan.regions.size() && body.instructions.size()==flow.instructions.size());
    limit_=std::min(budget.work,4096*n*(1+std::bit_width(n)));arena_.LimitWork(limit_);
    auto dispatch=raw_.Select(profile);raw_.Allocate();raw_.Connect(dispatch,profile,frame);
    member_.resize(flow.nodes.size());flow_raw_.resize(flow.nodes.size());
    reachable_.resize(raw_.Nodes().size());raw_flow_.resize(raw_.Nodes().size());
    for(const auto& m:r_.members){arena_.Step();Require(m.node.value<member_.size());member_[m.node.value]=true;}
    for(std::uint32_t n=0;n<flow.nodes.size();++n) {
      arena_.Step();const auto& node=flow.nodes[n];
      Require(node.instruction.value<flow.instructions.size() && node.successor_count<=node.successors.size());
      for(std::uint8_t e=0;e<node.successor_count;++e){arena_.Step();Require(node.successors[e].target.value<flow.nodes.size());}
      const auto phase=node.phase==ExceptionPhase::kBefore?0u:node.phase==ExceptionPhase::kCommit?1u:2u;
      flow_raw_[n]=node.instruction.value*RawEventGraph::kPhases+phase;
      Require(flow_raw_[n]<raw_.Nodes().size());raw_flow_[flow_raw_[n]]=ExceptionNodeId{n};
    }
    // Match immutable flow arcs against independently lifted raw dispatch.
    // A normal jump to handler code must not become a Catch entry.
    for(std::uint32_t n=0;n<flow.nodes.size();++n) {
      arena_.Step();const auto& source=flow.nodes[n];const auto& raw=raw_.Nodes()[flow_raw_[n]];
      Require(flow.instructions[source.instruction.value].pc==body.instructions[source.instruction.value].offset);
      std::array<bool,kMaxNormalSuccessors> matched{};
      for(std::uint8_t j=0;j<raw.count;++j) {
        arena_.Step();const auto target=raw.next[j];bool found=false;
        if(raw_.Nodes()[target].phase==RawEventPhase::kCallerExit) {
          found=(source.phase==ExceptionPhase::kBefore && source.exception && !source.exception->target) ||
              (source.phase==ExceptionPhase::kCommit && std::holds_alternative<ReturnOperation>(flow.instructions[source.instruction.value].operation));
        } else for(std::uint8_t e=0;e<source.successor_count;++e) {
          arena_.Step();if(!matched[e] && flow_raw_[source.successors[e].target.value]==target){matched[e]=true;found=true;break;}
        }
        Require(found);
      }
      for(std::uint8_t e=0;e<source.successor_count;++e){arena_.Step();Require(matched[e]);}
    }
    std::pmr::vector<std::uint32_t> todo(arena_.Resource());
    Require(!raw_.Nodes().empty());reachable_[0]=true;todo.push_back(0);
    // Both traversals prove original dispatch reachability, including exceptions;
    // a dead Commit is never an entry merely because it has a syntactic arc.
    for(std::size_t i=0;i<todo.size();++i) {
      arena_.Step();const auto& node=raw_.Nodes()[todo[i]];
      Require(!node.unknown,CloseOwnershipGap::kRawFlow);
      for(std::uint8_t j=0;j<node.count;++j) {
        arena_.Step();const auto k=reverse_?node.count-j-1:j;const auto next=node.next[k];
        if(!reachable_[next]){reachable_[next]=true;todo.push_back(next);}
      }
    }
  }
  CloseBoundaryProof Run() {
    Require(r_.outcomes.has_value());
    Reserve(out_.ports,r_.outcomes->leaving.size());
    std::pmr::set<CompletionExit,WorkLess> cuts(WorkLess{&arena_},arena_.Resource());
    for(const auto& exit:c_.exits){arena_.Step();cuts.insert(exit);}
    for(const auto& port:r_.outcomes->leaving) {
      arena_.Step();Require(port.entry<c_.entries.size(),CloseOwnershipGap::kPort);
      std::visit([&](const auto& exit){Require(exit.source.value<member_.size() && member_[exit.source.value],CloseOwnershipGap::kPort);},port.exit);
      const bool found=cuts.contains(port.exit);
      Require(found,CloseOwnershipGap::kPort);out_.ports.push_back(port);
    }
    // This table is the independently recomputed relation cut, not permission
    // for a live edge entering the middle of the close implementation.
    std::pmr::set<CompletionEdge,WorkLess> entries(WorkLess{&arena_},arena_.Resource());
    for(const auto& e:c_.entries){arena_.Step();entries.insert(e.entry.edge);}
    bool entry_ok=true;
    std::pmr::vector<CompletionEdge> dead(arena_.Resource());
    for(std::uint32_t n=0;n<flow_.nodes.size();++n) {
      arena_.Step();const auto& node=flow_.nodes[n];
      for(std::uint8_t e=0;e<node.successor_count;++e) {
        arena_.Step();const CompletionEdge edge{{n},e};const auto target=node.successors[e].target;
        if(!member_[target.value] || member_[n] || entries.contains(edge))continue;
        if(!reachable_[flow_raw_[n]])dead.push_back(edge);
        else {entry_ok=false;Missing(CloseCompletionError::kExternalEntry,CloseOwnershipGap::kRegionCoverage,target,edge);}
      }
    }
    Reserve(out_.unreachable_entries,dead.size());out_.unreachable_entries.assign(dead.begin(),dead.end());
    out_.entry_coverage=entry_ok?CloseProofState::kVerified:CloseProofState::kMissing;
    bool port_ok=true;
    for(const auto& member:r_.members) {
      arena_.Step();const auto& node=flow_.nodes[member.node.value];
      for(std::uint8_t e=0;e<node.successor_count;++e) {
        arena_.Step();if(!member_[node.successors[e].target.value] && !cuts.contains(CompletionExit{CompletionEdge{member.node,e}})) {
          port_ok=false;Missing(CloseCompletionError::kOwnership,CloseOwnershipGap::kPort,member.node,CompletionEdge{member.node,e});
        }
      }
    }
    out_.port_coverage=port_ok?CloseProofState::kVerified:CloseProofState::kMissing;
    Regions();Private();
    out_.peak_bytes=arena_.Peak();out_.work=arena_.Work();return std::move(out_);
  }
 private:
  void Require(bool value,CloseOwnershipGap gap=CloseOwnershipGap::kRegionCoverage) {
    if(!value)throw CloseCompletionFailure{CloseCompletionError::kCorrespondence,c_.protocol,c_.read,{}, {}, {}, {}, {}, {},gap};
  }
  void Missing(CloseCompletionError kind,CloseOwnershipGap gap,std::optional<ExceptionNodeId> node={},
      std::optional<CompletionEdge> edge={}) {
    const CloseCompletionFailure next{kind,c_.protocol,c_.read,{},node,edge,{}, {}, {},gap};
    const auto key=[](const CloseCompletionFailure& f){return std::tuple{f.kind!=CloseCompletionError::kExternalEntry,f.ownership_gap,f.kind,f.node,f.edge};};
    if(!out_.missing || key(next)<key(*out_.missing))out_.missing=next;
  }
  template<class T> void Reserve(std::vector<T>& v,std::size_t count) {
    if(count>arena_.Available()/sizeof(T))throw RawEventFailure{RawEventError::kResourceLimit,{}};
    arena_.Retain(count*sizeof(T));v.reserve(count);arena_.Retain((v.capacity()-count)*sizeof(T));
  }
  void Regions() {
    // The validated original ranges are nested/disjoint. Membership prefix
    // counts give O(1) overlap queries, without a region x instruction scan.
    std::pmr::vector<std::uint32_t> prefix(flow_.instructions.size()+1,0,arena_.Resource());
    for(const auto& m:r_.members){arena_.Step();prefix[flow_.nodes[m.node.value].instruction.value+1]=1;}
    for(std::size_t i=1;i<prefix.size();++i){arena_.Step();prefix[i]+=prefix[i-1];}
    std::pmr::vector<bool> associated(plan_.regions.size(),false,arena_.Resource());
    std::pmr::vector<bool> body_wrapper(plan_.regions.size(),false,arena_.Resource());
    std::optional<InstructionId> live_begin,live_end,body_begin,body_end;
    for(const auto& member:r_.members) {
      arena_.Step();if(!reachable_[flow_raw_[member.node.value]])continue;
      const auto site=flow_.nodes[member.node.value].instruction;
      if(!live_begin || site<*live_begin)live_begin=site;
      if(!live_end || *live_end<site)live_end=site;
    }
    for(const auto& region:scan_.regions) {
      arena_.Step();Require(region.region.value<associated.size());
      if(region.protocol==c_.protocol && region.role==IteratorRegionRole::kEnclosingHandler) {
        body_wrapper[region.region.value]=true;
        const auto range=plan_.layout.regions[region.region.value].body;
        if(!body_begin || range.begin<*body_begin)body_begin=range.begin;
        if(!body_end || *body_end<range.end)body_end=range.end;
      }
      if(region.protocol==c_.protocol && region.read.operation==c_.read) {
        Require(region.region.value<associated.size());associated[region.region.value]=true;
      }
    }
    Reserve(out_.regions,plan_.regions.size());
    Reserve(out_.enclosures,plan_.regions.size());
    Reserve(out_.protected_ranges,plan_.regions.size());
    const auto overlaps=[&](InstructionRange range) {
      Require(range.begin.value<=range.end.value && range.end.value<prefix.size());
      return prefix[range.end.value]!=prefix[range.begin.value];
    };
    bool region_ok=true;
    for(std::uint32_t t=0;t<plan_.layout.regions.size();++t) {
      arena_.Step();const auto& region=plan_.layout.regions[t];
      if(!overlaps(region.body) && !overlaps(region.handler))continue;
      bool role=false;std::optional<IteratorRegionRole> selected;
      if(associated[t])for(const auto& candidate:scan_.regions) {
        arena_.Step();if(candidate.protocol!=c_.protocol || candidate.read.operation!=c_.read || candidate.region.value!=t)continue;
        switch(candidate.role) {
          case IteratorRegionRole::kReturnProtection:
            for(const auto& member:r_.members) {
              arena_.Step();const auto& n=flow_.nodes[member.node.value];
              if(n.phase==ExceptionPhase::kBefore && member.role==CloseMemberRole::kReturnRead && n.exception &&
                  n.exception->target && n.exception->target->region.value==t &&
                  region.body.begin.value<=n.instruction.value && n.instruction.value<region.body.end.value){role=true;selected=candidate.role;}
            }
            break;
          case IteratorRegionRole::kEnclosingHandler:
            for(const auto& e:c_.entries) {
              arena_.Step();if(e.catcher && flow_.nodes[e.catcher->value].instruction==region.handler.begin &&
                  e.entry.pending.kind==PendingCompletionKind::kThrow){role=true;selected=candidate.role;}
            }
            break;
        }
      }
      if(!associated[t] || !role) {
        // Another close copy may own THIS acquisition's body wrapper. A
        // break/return copy inside its protected body is not a user try and
        // must not claim the wrapper/handler. Complete coverage is checked by
        // the wrapper owner's independently recomputed split certificate.
        if(body_wrapper[t] && entry_dominators_ && live_begin && live_end &&
            region.body.begin<=*live_begin && *live_end<region.body.end)continue;
        // A user try may surround a whole sealed close statement. Its
        // handler stays ordinary: only complete protected containment (and
        // disjoint handler code) authorizes composition, not partial overlap.
        std::optional<InstructionId> site;
        if(c_.protocol.value<scan_.candidates.size()) {
          const auto acquisition=scan_.candidates[c_.protocol.value].acquisition.before;
          Require(acquisition.value<flow_.nodes.size());site=flow_.nodes[acquisition.value].instruction;
        }
        const auto contains=[&](ExceptionNodeId n) {
          Require(n.value<flow_.nodes.size());const auto i=flow_.nodes[n.value].instruction;
          return region.body.begin<=i && i<region.body.end;
        };
        if (!overlaps(region.handler) && site && body_begin && body_end &&
            region.body.begin<=*site && *site<region.body.end &&
            region.body.begin<=*body_begin && *body_end<=region.body.end &&
            contains(r_.iteration.advance) && contains(r_.iteration.exhausted.source) &&
            (!r_.iteration.value || contains(*r_.iteration.value)) &&
            prefix[region.body.end.value]-prefix[region.body.begin.value]==prefix.back()) {
          out_.enclosures.push_back({t});
          continue;
        }
        region_ok=false;
        Missing(CloseCompletionError::kOwnership,CloseOwnershipGap::kUserTry);
        if(out_.missing && out_.missing->ownership_gap==CloseOwnershipGap::kUserTry && !out_.missing->region)out_.missing->region=TryId{t};
        continue;
      }
      bool covered=true;
      if(*selected==IteratorRegionRole::kEnclosingHandler) {
        auto body=ProveCloseLoopBody(flow_,plan_,c_,{t},{arena_.Available(),limit_-arena_.Work()},reverse_,entry_dominators_);
        if(const auto* f=std::get_if<CloseCompletionFailure>(&body))throw *f;
        auto proof=std::get<CloseProtectedRange>(std::move(body));
        arena_.Observe(proof.peak_bytes);arena_.Step(proof.work);
        arena_.Retain(proof.ordinary.capacity()*sizeof(ExceptionNodeId)+proof.transport.capacity()*sizeof(ExceptionNodeId)+proof.protocol.capacity()*sizeof(ExceptionNodeId)+
            proof.dispatch.capacity()*sizeof(CompletionEdge)+proof.exits.capacity()*sizeof(CompletionEdge));
        if(proof.missing) {
          covered=false;Missing(proof.missing->kind,*proof.missing->ownership_gap,proof.missing->node,proof.missing->edge);
          if(out_.missing && !out_.missing->region)out_.missing->region=TryId{t};
        }
        for(const auto n:proof.ordinary) {
          arena_.Step();if(member_[n.value]){covered=false;Missing(CloseCompletionError::kOwnership,CloseOwnershipGap::kBodyRange,n);}
        }
        out_.protected_ranges.push_back(std::move(proof));
      } else out_.protected_ranges.push_back(CloseProtectedRange{TryId{t},ProtectedRangeRole::kCloseCode});
      // A body wrapper owns dispatch + handler, never ordinary protected ops.
      // An inner return-protection region still owns both its code and handler.
      for(const auto range:{region.body,region.handler}) {
        if(*selected==IteratorRegionRole::kEnclosingHandler && range==region.body)continue;
        for(auto i=range.begin.value;i<range.end.value;++i) {
          arena_.Step();const auto before=i*RawEventGraph::kPhases;
          if(reachable_[before] && (!raw_flow_[before] || !member_[raw_flow_[before]->value])) {
            covered=false;
            Missing(CloseCompletionError::kOwnership,CloseOwnershipGap::kRegionCoverage,raw_flow_[before]);
            if(out_.missing && out_.missing->kind==CloseCompletionError::kOwnership && out_.missing->ownership_gap==CloseOwnershipGap::kRegionCoverage && !out_.missing->region)out_.missing->region=TryId{t};
            break;
          }
        }
      }
      if(covered)out_.regions.push_back({t});else region_ok=false;
    }
    out_.region_coverage=region_ok?CloseProofState::kVerified:CloseProofState::kMissing;
  }
  bool Reads(const RegisterEffect& effect,ClosePrivateSlot slot) {
    for(std::uint8_t i=0;i<effect.read_count;++i){arena_.Step();if(slot.reg){if(const auto* r=std::get_if<abc::RegisterId>(&effect.reads[i]);r && *r==*slot.reg)return true;}
      else if(std::holds_alternative<Accumulator>(effect.reads[i]))return true;}
    return slot.reg && effect.range && slot.reg->value>=effect.range->first.value &&
        slot.reg->value-effect.range->first.value<effect.range->count;
  }
  void Private() {
    // Every physical slot (including acc) written by close code is conservatively private;
    // no special-case register list or compiler layout is an admission rule.
    // Catch defines acc on exception entry; a normal jump must not receive that kill.
    bool private_ok=true;
    for(const auto& m:r_.members) {
      arena_.Step();const auto& node=raw_.Nodes()[flow_raw_[m.node.value]];
      if(std::holds_alternative<LexicalStore>(flow_.instructions[flow_.nodes[m.node.value].instruction.value].operation)) {
        private_ok=false;Missing(CloseCompletionError::kOwnership,CloseOwnershipGap::kPrivateState,m.node);
      }
      if(node.registers.write) {
        if(const auto* slot=std::get_if<abc::RegisterId>(&*node.registers.write))private_.insert({*slot});
        else private_.insert(ClosePrivateSlot{});
      }
    }
    Reserve(out_.private_slots,private_.size());out_.private_slots.assign(private_.begin(),private_.end());
    using State=std::pair<std::uint32_t,ClosePrivateSlot>;
    std::pmr::set<State,WorkLess> seen(WorkLess{&arena_},arena_.Resource());
    std::pmr::vector<State> todo(arena_.Resource());
    const auto add=[&](State s){arena_.Step();if(seen.insert(s).second)todo.push_back(s);};
    for(const auto& port:out_.ports)if(const auto* edge=std::get_if<CompletionEdge>(&port.exit)) {
      arena_.Step();const auto target=flow_.nodes[edge->source.value].successors[edge->successor].target;
      for(const auto slot:private_){arena_.Step();add({flow_raw_[target.value],slot});}
    }
    for(std::size_t i=0;i<todo.size();++i) {
      arena_.Step();const auto [id,slot]=todo[i];const auto& node=raw_.Nodes()[id];
      if(node.unknown) {private_ok=false;Missing(CloseCompletionError::kOwnership,CloseOwnershipGap::kRawFlow);continue;}
      const auto witness=raw_flow_[id];
      bool protocol_read=false;
      if(witness && entry_dominators_) {
        const auto& members=entry_dominators_->members;
        protocol_read=std::binary_search(members.begin(),members.end(),CloseBodyMember{c_.protocol,*witness},
            [&](const auto& a,const auto& b){arena_.Step();return a<b;});
      }
      if(Reads(node.registers,slot) && !protocol_read) {
        private_ok=false;
        // Map a raw event to its real immutable flow identity; no made-up PC.
        Missing(CloseCompletionError::kOwnership,CloseOwnershipGap::kLiveOut,witness);continue;
      }
      if(node.registers.write) {
        if(slot.reg) {if(const auto* r=std::get_if<abc::RegisterId>(&*node.registers.write);r && *r==*slot.reg)continue;}
        else if(std::holds_alternative<Accumulator>(*node.registers.write))continue;
      }
      for(std::uint8_t j=0;j<node.count;++j) {
        arena_.Step();const auto k=reverse_?node.count-j-1:j;add({node.next[k],slot});
      }
    }
    out_.private_state=private_ok?CloseProofState::kVerified:CloseProofState::kMissing;
  }
  const ExceptionFlow& flow_;const ExceptionPlan& plan_;const IteratorProtocolScan& scan_;
  const DerivedClose& c_;const CloseCompletionRegion& r_;
  RawEventArena arena_;RawEventGraph raw_;
  std::pmr::vector<bool> member_,reachable_;
  std::pmr::vector<std::uint32_t> flow_raw_;
  std::pmr::vector<std::optional<ExceptionNodeId>> raw_flow_;
  std::pmr::set<ClosePrivateSlot,WorkLess> private_;
  bool reverse_;const CloseBodyIndex* entry_dominators_;std::uint64_t limit_=0;CloseBoundaryProof out_;
};
}
std::variant<CloseBoundaryProof,CloseCompletionFailure> ProveCloseBoundary(const abc::MethodBody& body,
    abc::ProfileId profile,const ExceptionFlow& flow,std::uint64_t frame,const ExceptionPlan& plan,const IteratorProtocolScan& scan,
    const DerivedClose& close,const CloseCompletionRegion& region,SemanticBudget budget,bool reverse,const CloseBodyIndex* entry_dominators) {
  try {return Boundary(body,profile,flow,frame,plan,scan,close,region,budget,reverse,entry_dominators).Run();}
  catch(const CloseCompletionFailure& f){return f;}
  catch(const RawEventFailure& f){return CloseCompletionFailure{f.kind==RawEventError::kResourceLimit?CloseCompletionError::kResourceLimit:
      f.kind==RawEventError::kWorkLimit?CloseCompletionError::kWorkLimit:CloseCompletionError::kCorrespondence,close.protocol,close.read};}
  catch(const std::bad_alloc&){return CloseCompletionFailure{CloseCompletionError::kResourceLimit,close.protocol,close.read};}
  catch(const std::length_error&){return CloseCompletionFailure{CloseCompletionError::kResourceLimit,close.protocol,close.read};}
}
}
