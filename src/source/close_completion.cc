#include "close_completion.h"
#include "close_boundary.h"

#include "../analysis/idf_scratch.h"
#include "../analysis/value_phi.h"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
struct Quota {
  detail::IdfScratch memory;
  std::uint64_t left, initial;
  explicit Quota(SemanticBudget b):memory(b.bytes),left(b.work),initial(b.work) {}
  void Step(std::uint64_t n=1) {
    if(n>left)throw CloseCompletionFailure{CloseCompletionError::kWorkLimit};left-=n;
  }
  void Require(bool b,CloseCompletionError kind=CloseCompletionError::kCorrespondence) {
    if(!b)throw CloseCompletionFailure{kind};
  }
  SemanticBudget Remaining() const {return {memory.Available(),left};}
  template<class T> void Reserve(std::vector<T>& v,std::size_t count) {
    Require(count<=UINT32_MAX && count<=memory.Available()/sizeof(T),CloseCompletionError::kResourceLimit);
    memory.Retain(count*sizeof(T));v.reserve(count);memory.Retain((v.capacity()-count)*sizeof(T));
  }
};
std::size_t Retained(const CloseEntries& entries) {
  std::size_t bytes=entries.closes.capacity()*sizeof(DerivedClose);
  for(const auto& c:entries.closes)bytes+=c.entries.capacity()*sizeof(DerivedCloseEntry)+
      c.exits.capacity()*sizeof(CompletionExit)+c.relation.arrivals.capacity()*sizeof(CompletionArrival)+
      c.relation.leaving.capacity()*sizeof(CompletionLeaving);
  return bytes;
}
class Proof {
 public:
  Proof(const abc::MethodBody& body,abc::ProfileId profile,const ValueProgram& g,const PhasedValueInput& in,
      const ExceptionPlan& plan,const IteratorKeys& keys,const IteratorProtocolScan& scan,
      SemanticBudget budget,bool verify,SourceContract contract):body_(body),profile_(profile),g_(g),in_(in),plan_(plan),keys_(keys),scan_(scan),q_(budget),
      before_(&q_.memory),commit_(&q_.memory),op_(&q_.memory),calls_(&q_.memory),
      pred_offsets_(&q_.memory),preds_(&q_.memory),marks_(&q_.memory),verify_(verify),contract_(contract) {
    constexpr std::uint64_t kWorkPerInput=4096;
    const auto n=std::uint64_t(in.flow.nodes.size())+g.uses.size()+g.values.size()+scan.regions.size()+1;
    q_.Require(n<=UINT32_MAX,CloseCompletionError::kResourceLimit);
    q_.left=q_.initial=std::min(q_.left,kWorkPerInput*n*(1+std::bit_width(n)));
    auto derived=DeriveCloseEntries(body,profile,g,in,plan,keys,scan,q_.Remaining());
    if(const auto* f=std::get_if<CloseEntryFailure>(&derived))Fail(*f);
    entries_=std::get<CloseEntries>(std::move(derived));
    q_.memory.Observe(entries_.peak_bytes);q_.Step(entries_.work);q_.memory.Retain(Retained(entries_));
    if(verify_) {
      // The independent backwards entry/guard derivation is essential: exit
      // tuples alone cannot establish what completion entered this close.
      q_.memory.Observe(q_.memory.Available());
      const auto checked=VerifyCloseEntries(body,profile,g,in,plan,keys,scan,entries_,q_.Remaining());
      if(const auto* f=std::get_if<CloseEntryFailure>(&checked))Fail(*f);
      q_.Step(std::get<std::uint64_t>(checked));
    }
    auto ids=AnalyzeValueIdentities(g,q_.Remaining());
    if(const auto* f=std::get_if<ValueSimplifyError>(&ids))
      q_.Require(false,*f==ValueSimplifyError::kResourceLimit?CloseCompletionError::kResourceLimit:
          *f==ValueSimplifyError::kWorkLimit?CloseCompletionError::kWorkLimit:CloseCompletionError::kInput);
    ids_=std::get<ValueIdentities>(std::move(ids));q_.memory.Retain(ids_.allocated_bytes);q_.Step(ids_.work);
    Index();
  }
  CloseCompletionSummary Run() {
    CloseCompletionSummary out{keys_.method,g_.revision,g_.cfg_revision};
    std::size_t count=0;for(const auto& c:entries_.closes){q_.Step();count+=c.entries.size();}
    q_.Reserve(out.entries,count);q_.Reserve(out.regions,entries_.closes.size());
    for(const auto& c:entries_.closes) {
      auto region=Members(c);
      const auto reference=Reference(c,region);
      if(reference.state==CloseProofState::kVerified){Outcomes(c,region);RefineMembers(region);}
      const auto ownership=Ownership(c,region);
      for(std::uint32_t i=0;i<c.entries.size();++i) {
        q_.Step();CloseCompletionEntry e{c.protocol,c.read,c.entries[i].entry.edge,c.entries[i].entry.pending};
        At(e,CloseObligation::kGuard)={CloseProofState::kVerified,{}};
        At(e,CloseObligation::kReference)=reference;
        At(e,CloseObligation::kPriority)=Priority(c,i,region);
        At(e,CloseObligation::kOwnership)=ownership;
        // A nested protocol requires a correlated bridge across its boundary.
        // Never turn an unanalysed nesting into an empty (vacuously true) proof.
        if(scan_.candidates.size()==1)At(e,CloseObligation::kNested)={CloseProofState::kVerified,{}};
        else At(e,CloseObligation::kNested)=Missing(c,CloseCompletionError::kNested,{},i);
        out.entries.push_back(std::move(e));
      }
      out.regions.push_back(std::move(region));
    }
    out.peak_bytes=q_.memory.Peak();out.work=q_.initial-q_.left;return out;
  }
 private:
  [[noreturn]] void Fail(const CloseEntryFailure& f) {
    throw CloseCompletionFailure{f.kind==CloseEntryError::kResourceLimit?CloseCompletionError::kResourceLimit:
        f.kind==CloseEntryError::kWorkLimit?CloseCompletionError::kWorkLimit:CloseCompletionError::kCompletion,
        f.protocol,f.read,{},f.node,{},f};
  }
  static CloseObligationResult& At(CloseCompletionEntry& e,CloseObligation o) {return e.obligations[static_cast<std::size_t>(o)];}
  CloseObligationResult Missing(const DerivedClose& c,CloseCompletionError reason,
      std::optional<ExceptionNodeId> node={},std::optional<std::uint32_t> entry={},
      std::optional<CompletionEdge> edge={}) const {
    return {CloseProofState::kMissing,CloseCompletionFailure{reason,c.protocol,c.read,entry,node,edge}};
  }
  const ExceptionNode& Node(ExceptionNodeId n) const {return in_.flow.nodes[n.value];}
  const SemanticInstruction& Instruction(ExceptionNodeId n) const {return in_.flow.instructions[Node(n).instruction.value];}
  ValueId Identity(ValueId v) const {return ids_.canonical[v.value];}
  std::optional<ValueId> Use(OpId op,OperandRole role) {
    const auto& o=g_.ops[op.value];std::optional<ValueId> result;
    for(std::uint32_t i=0;i<o.use_count;++i) {
      q_.Step();const auto& u=g_.uses[o.first_use.value+i];if(u.role!=role)continue;
      const auto* value=std::get_if<ValueId>(&u.operand);
      if(!value || result)return {};result=Identity(*value);
    }
    return result;
  }
  void Index() {
    before_.resize(in_.flow.instructions.size());commit_.resize(in_.flow.instructions.size());
    op_.resize(in_.flow.instructions.size());marks_.resize(in_.flow.nodes.size());
    calls_.resize(g_.values.size());pred_offsets_.resize(in_.flow.nodes.size()+1);
    q_.Step(in_.flow.instructions.size()*3+in_.flow.nodes.size()+g_.values.size());
    for(std::uint32_t n=0;n<in_.flow.nodes.size();++n) {
      q_.Step();const auto& node=Node({n});
      if(node.phase==ExceptionPhase::kBefore)before_[node.instruction.value]=ExceptionNodeId{n};
      if(node.phase==ExceptionPhase::kCommit)commit_[node.instruction.value]=ExceptionNodeId{n};
      for(std::uint8_t e=0;e<node.successor_count;++e){q_.Step();++pred_offsets_[node.successors[e].target.value+1];}
    }
    for(std::size_t n=1;n<pred_offsets_.size();++n){q_.Step();pred_offsets_[n]+=pred_offsets_[n-1];}
    preds_.resize(pred_offsets_.back());std::pmr::vector<std::uint32_t> cursor(pred_offsets_,&q_.memory);
    for(std::uint32_t n=0;n<in_.flow.nodes.size();++n)for(std::uint8_t e=0;e<Node({n}).successor_count;++e) {
      q_.Step();preds_[cursor[Node({n}).successors[e].target.value]++]={{n},e};
    }
    for(std::uint32_t o=0;o<g_.ops.size();++o) {
      q_.Step();const auto& v=g_.ops[o];const auto node=g_.phases[v.block.value].input;
      q_.Require(Node(node).phase==ExceptionPhase::kBefore);op_[Node(node).instruction.value]=OpId{o};
      if(std::holds_alternative<ValueInvoke>(v.operation))if(const auto callee=Use({o},OperandRole::kCallee)) {
        auto& call=calls_[callee->value];if(call.count++==0)call.op=OpId{o};
      }
    }
  }
  bool Cut(const std::pmr::vector<CompletionEdge>& cuts,CompletionEdge e) {
    q_.Step(1+std::bit_width(cuts.size()));return std::binary_search(cuts.begin(),cuts.end(),e);
  }
  CloseCompletionRegion Members(const DerivedClose& c) {
    CloseCompletionRegion r{c.protocol,c.read};
    // Per-close sparse regions are admitted by a shared N log N work grant;
    // dense overlaps refuse resources instead of becoming quadratic admitted work.
    std::pmr::vector<CompletionEdge> cuts(&q_.memory);
    std::pmr::vector<ExceptionNodeId> callers(&q_.memory),queue(&q_.memory);
    for(const auto& x:c.exits){q_.Step();if(const auto* e=std::get_if<CompletionEdge>(&x))cuts.push_back(*e);else callers.push_back(std::get<CompletionCallerExit>(x).source);}
    Sort(cuts);Sort(callers);
    const auto stamp=++generation_;q_.Require(stamp!=0,CloseCompletionError::kResourceLimit);
    const auto add=[&](ExceptionNodeId n){q_.Step();if(marks_[n.value]!=stamp){marks_[n.value]=stamp;queue.push_back(n);}};
    for(const auto& e:c.entries)add(Node(e.entry.edge.source).successors[e.entry.edge.successor].target);
    std::pmr::vector<CompletionEdge> edges(&q_.memory);
    for(std::size_t n=0;n<queue.size();++n) {
      const auto id=queue[n];const auto& node=Node(id);q_.Step();
      q_.Step(1+std::bit_width(callers.size()));
      const bool terminal=std::binary_search(callers.begin(),callers.end(),id);
      for(std::uint8_t e=0;e<node.successor_count;++e) {
        q_.Step();const CompletionEdge edge{id,e};edges.push_back(edge);
        if(!terminal && !Cut(cuts,edge))add(node.successors[e].target);
      }
    }
    Sort(queue);Sort(edges);q_.Reserve(r.members,queue.size());q_.Reserve(r.edges,edges.size());
    for(auto n:queue){q_.Step();r.members.push_back({n,Role(c,n)});}r.edges.assign(edges.begin(),edges.end());
    return r;
  }
  template<class T> void Sort(std::pmr::vector<T>& v) {
    std::sort(v.begin(),v.end(),[&](const T& a,const T& b){q_.Step();return a<b;});
  }
  CloseMemberRole Role(const DerivedClose& c,ExceptionNodeId n) {
    const auto& node=Node(n);if(node.phase==ExceptionPhase::kCatch)return CloseMemberRole::kCatch;
    const auto op=op_[node.instruction.value];if(!op)return CloseMemberRole::kUnproved;
    if(*op==c.read)return CloseMemberRole::kReturnRead;
    const auto& v=g_.ops[op->value];
    if(const auto* invoke=std::get_if<ValueInvoke>(&v.operation)) {
      const auto callee=Use(*op,OperandRole::kCallee);
      if(g_.ops[c.read.value].result && callee==Identity(*g_.ops[c.read.value].result) &&
          invoke->receiver==InvokeReceiver::kExplicit && invoke->arguments==0)return CloseMemberRole::kReturnCall;
      return CloseMemberRole::kUnproved;
    }
    if(std::holds_alternative<ValueObjectCheck>(v.operation)) {
      const auto& read=g_.ops[c.read.value];
      if(read.result) {
        const auto call=calls_[Identity(*read.result).value];
        if(call.count==1 && call.op) {
          const auto result=g_.ops[call.op->value].result;
          // A phi that also contains an initialized undefined is not the
          // call result without an edge-qualified successful-call proof.
          if(result && Use(*op,OperandRole::kInput)==Identity(*result))return CloseMemberRole::kResultCheck;
        }
      }
      return CloseMemberRole::kUnproved;
    }
    if(std::holds_alternative<ValueThrow>(v.operation)||std::holds_alternative<ValueReturn>(v.operation))return CloseMemberRole::kCompletion;
    if(v.normal==LanguageEffect::kNone && v.before_throw==LanguageEffect::kNone && v.exceptional==ExceptionalCompletion::kImpossible)
      return CloseMemberRole::kScalar;
    // Unknown comparisons may execute user code. Keeping their exit relation
    // conservative is not permission to absorb their effect into IteratorClose.
    return CloseMemberRole::kUnproved;
  }
  void RefineMembers(CloseCompletionRegion& r) {
    q_.Require(r.outcomes.has_value());
    // Complete entry coverage and the independent relational replay establish
    // the conjunction. One favourable exit is NOT proof for an entire node.
    for(auto& m:r.members)if(m.role==CloseMemberRole::kUnproved) {
      q_.Step();const auto before=before_[Node(m.node).instruction.value];
      if(!before)continue;
      const auto& facts=r.outcomes->events;
      const auto fact=std::lower_bound(facts.begin(),facts.end(),*before,
          [&](const CloseEventFact& a,ExceptionNodeId b){q_.Step();return a.node<b;});
      if(fact==facts.end() || fact->node!=*before)m.role=CloseMemberRole::kInfeasible;
      else if(fact->proved)switch(fact->kind) {
        case CloseEventKind::kComparison:m.role=CloseMemberRole::kComparison;break;
        case CloseEventKind::kResultCheck:m.role=CloseMemberRole::kResultCheck;break;
        case CloseEventKind::kScalar:m.role=CloseMemberRole::kScalar;break;
        case CloseEventKind::kOther:break;
      }
    }
  }
  CloseObligationResult Reference(const DerivedClose& c,CloseCompletionRegion& r) {
    const auto& read=g_.ops[c.read.value];
    if(!read.result || c.protocol.value>=scan_.candidates.size())return Missing(c,CloseCompletionError::kReference);
    const auto base=Use(c.read,OperandRole::kBase);
    const auto iterator=Identity(scan_.candidates[c.protocol.value].iterator);
    if(!base || *base!=iterator)return Missing(c,CloseCompletionError::kReference);
    const auto call=calls_[Identity(*read.result).value];
    if(call.count!=1 || !call.op)return Missing(c,CloseCompletionError::kReference);
    r.call=call.op;
    const auto* invoke=std::get_if<ValueInvoke>(&g_.ops[call.op->value].operation);
    if(!invoke || invoke->receiver!=InvokeReceiver::kExplicit || invoke->arguments!=0 ||
        Use(*call.op,OperandRole::kReceiver)!=iterator)return Missing(c,CloseCompletionError::kReference);
    const auto call_node=g_.phases[g_.ops[call.op->value].block.value].input;
    const auto found=std::lower_bound(r.members.begin(),r.members.end(),call_node,
        [&](const CloseMember& m,ExceptionNodeId n){q_.Step();return m.node<n;});
    if(found==r.members.end() || found->node!=call_node)return Missing(c,CloseCompletionError::kReference,call_node);
    return {CloseProofState::kVerified,{}};
  }
  void Outcomes(const DerivedClose& c,CloseCompletionRegion& r) {
    q_.Require(r.call.has_value());
    std::pmr::vector<CompletionEntry> incoming(&q_.memory);
    for(const auto& e:c.entries){q_.Step();incoming.push_back(e.entry);}
    const CloseOutcomeQuery query{g_.phases[g_.ops[c.read.value].block.value].input,
        g_.phases[g_.ops[r.call->value].block.value].input,ExternalExceptionNoHole(contract_)};
    auto result=AnalyzeCloseOutcomeRelation(body_,profile_,in_.frame.registers,in_.flow,in_.ssa,
        query,incoming,c.exits,{q_.Remaining()});
    if(const auto* f=std::get_if<CompletionRelationFailure>(&result))
      throw CloseCompletionFailure{f->kind==CompletionRelationError::kResourceLimit?CloseCompletionError::kResourceLimit:
          f->kind==CompletionRelationError::kWorkLimit?CloseCompletionError::kWorkLimit:CloseCompletionError::kCorrespondence,
          c.protocol,c.read,f->entry,f->node,f->edge};
    r.outcomes=std::get<CloseOutcomeRelation>(std::move(result));
    q_.memory.Observe(r.outcomes->peak_bytes);q_.Step(r.outcomes->work);
    q_.memory.Retain(r.outcomes->arrivals.capacity()*sizeof(CompletionArrival)+r.outcomes->leaving.capacity()*sizeof(CloseOutcomeLeaving)+
        r.outcomes->comparisons.capacity()*sizeof(NonThrowingComparison)+r.outcomes->catch_payloads.capacity()*sizeof(CatchPayloadFact)+r.outcomes->events.capacity()*sizeof(CloseEventFact));
    if(verify_) {
      const auto verified=VerifyCloseOutcomeRelation(body_,profile_,in_.frame.registers,in_.flow,in_.ssa,
          query,incoming,c.exits,*r.outcomes,{q_.Remaining()});
      if(const auto* f=std::get_if<CompletionRelationFailure>(&verified))
        throw CloseCompletionFailure{f->kind==CompletionRelationError::kResourceLimit?CloseCompletionError::kResourceLimit:
            f->kind==CompletionRelationError::kWorkLimit?CloseCompletionError::kWorkLimit:CloseCompletionError::kCorrespondence,
            c.protocol,c.read,f->entry,f->node,f->edge};
      const auto cost=std::get<CompletionRelationCost>(verified);
      q_.memory.Observe(cost.peak_bytes);q_.Step(cost.work);
    }
  }
  CloseObligationResult PriorityGap(const DerivedClose& c,std::uint32_t entry,ClosePriorityReason reason,
      std::optional<ExceptionNodeId> node={},std::optional<CloseOutcomeGap> gap={}) {
    auto result=Missing(c,CloseCompletionError::kClosePriority,node,entry);
    result.missing->priority=reason;result.missing->outcome_gap=gap;return result;
  }
  CloseObligationResult Priority(const DerivedClose& c,std::uint32_t entry,const CloseCompletionRegion& r) {
    const auto& pending=c.entries[entry].entry.pending;bool leaves=false;
    if(pending.kind!=PendingCompletionKind::kThrow) {
      if(!r.outcomes)return PriorityGap(c,entry,ClosePriorityReason::kOutcomeUnproved);
      const auto& exits=r.outcomes->leaving;
      const auto first=std::lower_bound(exits.begin(),exits.end(),entry,
          [&](const CloseOutcomeLeaving& a,std::uint32_t b){q_.Step();return a.entry<b;});
      bool covered=false;std::optional<CloseObligationResult> missing;
      for(auto it=first;it!=exits.end() && it->entry==entry;++it) {
        q_.Step();covered=true;
        if(const auto mismatch=ClosePriorityMismatch(pending,*it);mismatch && !missing) {
          const auto& outcome=it->outcome;
          missing=PriorityGap(c,entry,*mismatch,outcome.uncertain_at?outcome.uncertain_at:outcome.event,
              outcome.gap==CloseOutcomeGap::kNone?std::nullopt:std::optional{outcome.gap});
        }
      }
      if(missing)return *missing;
      if(!covered)return PriorityGap(c,entry,ClosePriorityReason::kMissingExit);
      return {CloseProofState::kVerified,{}};
    }
    // Partition by entry instead of rescanning the complete relation per entry.
    const auto begin=std::lower_bound(c.relation.leaving.begin(),c.relation.leaving.end(),entry,
        [&](const CompletionLeaving& a,std::uint32_t b){q_.Step();return a.entry<b;});
    for(auto it=begin;it!=c.relation.leaving.end() && it->entry==entry;++it) {
      q_.Step();leaves=true;if(it->pending==pending)continue;
      return Missing(c,CloseCompletionError::kClosePriority,{},entry);
    }
    // An exit-free cycle is not evidence that a close completes or propagates.
    if(!leaves)return Missing(c,CloseCompletionError::kCompletion,{},entry);
    return {CloseProofState::kVerified,{}};
  }
  CloseObligationResult Ownership(const DerivedClose& c,CloseCompletionRegion& r) {
    if(!r.outcomes)return Missing(c,CloseCompletionError::kOwnership);
    auto proof=ProveCloseBoundary(body_,profile_,in_.flow,in_.frame.registers,plan_,scan_,c,r,q_.Remaining(),verify_);
    if(const auto* f=std::get_if<CloseCompletionFailure>(&proof))throw *f;
    r.boundary=std::get<CloseBoundaryProof>(std::move(proof));
    q_.memory.Observe(r.boundary->peak_bytes);q_.Step(r.boundary->work);
    q_.memory.Retain(r.boundary->ports.capacity()*sizeof(CloseOutcomeLeaving)+
        r.boundary->unreachable_entries.capacity()*sizeof(CompletionEdge)+
        r.boundary->private_slots.capacity()*sizeof(ClosePrivateSlot)+
        r.boundary->regions.capacity()*sizeof(TryId));
    if(r.boundary->missing)return {CloseProofState::kMissing,r.boundary->missing};
    for(const auto& m:r.members) {
      q_.Step();if(m.role==CloseMemberRole::kUnproved) {
        auto missing=Missing(c,CloseCompletionError::kOwnership,m.node);
        missing.missing->ownership_gap=CloseOwnershipGap::kEventCoverage;return missing;
      }
    }
    return {CloseProofState::kVerified,{}};
  }

  const abc::MethodBody& body_;abc::ProfileId profile_;const ValueProgram& g_;const PhasedValueInput& in_;
  const ExceptionPlan& plan_;const IteratorKeys& keys_;const IteratorProtocolScan& scan_;
  Quota q_;CloseEntries entries_;ValueIdentities ids_;
  std::pmr::vector<std::optional<ExceptionNodeId>> before_,commit_;
  std::pmr::vector<std::optional<OpId>> op_;
  struct Calls {std::uint32_t count=0;std::optional<OpId> op;};
  std::pmr::vector<Calls> calls_;
  std::pmr::vector<std::uint32_t> pred_offsets_;
  std::pmr::vector<CompletionEdge> preds_;
  std::pmr::vector<std::uint32_t> marks_;
  std::uint32_t generation_=0;
  bool verify_;
  SourceContract contract_;
};
std::variant<CloseCompletionSummary,CloseCompletionFailure> Build(const abc::MethodBody& body,abc::ProfileId profile,
    const ValueProgram& g,const PhasedValueInput& in,const ExceptionPlan& plan,const IteratorKeys& keys,
    const IteratorProtocolScan& scan,SemanticBudget budget,bool verify,SourceContract contract) {
  try {return Proof(body,profile,g,in,plan,keys,scan,budget,verify,contract).Run();}
  catch(const CloseCompletionFailure& f){return f;}
  catch(const std::bad_alloc&){return CloseCompletionFailure{CloseCompletionError::kResourceLimit};}
  catch(const std::length_error&){return CloseCompletionFailure{CloseCompletionError::kResourceLimit};}
}
}
std::optional<ClosePriorityReason> ClosePriorityMismatch(const analysis::PendingCompletion& entry,
    const analysis::CloseOutcomeLeaving& leaving) {
  using namespace analysis;
  if(entry.kind==PendingCompletionKind::kThrow)
    return leaving.pending==entry?std::nullopt:std::optional{ClosePriorityReason::kExpectedOriginal};
  const auto& outcome=leaving.outcome;
  if(outcome.gap!=CloseOutcomeGap::kNone)return outcome.gap==CloseOutcomeGap::kSentinelPath?
      ClosePriorityReason::kSentinelPath:ClosePriorityReason::kOutcomeUnproved;
  switch(outcome.kind) {
    case CloseOutcomeKind::kUnproved:return ClosePriorityReason::kOutcomeUnproved;
    case CloseOutcomeKind::kSuccessObject:case CloseOutcomeKind::kMissingOrNullish:
      return leaving.pending==entry?std::nullopt:std::optional{ClosePriorityReason::kExpectedOriginal};
    case CloseOutcomeKind::kGetterThrow:case CloseOutcomeKind::kCallThrow:
    case CloseOutcomeKind::kPrimitiveResult: {
      const auto reason=outcome.kind==CloseOutcomeKind::kPrimitiveResult?
          ClosePriorityReason::kExpectedTypeError:ClosePriorityReason::kExpectedCloseError;
      if(!outcome.event || !outcome.exception ||
          *outcome.exception!=CompletionPayload{CompletionExceptionValue{*outcome.event}} ||
          leaving.pending.kind!=PendingCompletionKind::kThrow || leaving.pending.payload!=*outcome.exception)return reason;
      return {};
    }
  }
  std::terminate();
}
std::variant<CloseCompletionSummary,CloseCompletionFailure> AnalyzeCloseCompletion(const abc::MethodBody& body,
    abc::ProfileId profile,const ValueProgram& g,const PhasedValueInput& in,const ExceptionPlan& plan,
    const IteratorKeys& keys,const IteratorProtocolScan& scan,SemanticBudget budget,SourceContract contract) {
  return Build(body,profile,g,in,plan,keys,scan,budget,false,contract);
}
std::variant<std::uint64_t,CloseCompletionFailure> VerifyCloseCompletion(const abc::MethodBody& body,
    abc::ProfileId profile,const ValueProgram& g,const PhasedValueInput& in,const ExceptionPlan& plan,
    const IteratorKeys& keys,const IteratorProtocolScan& scan,const CloseCompletionSummary& supplied,SemanticBudget budget,SourceContract contract) {
  auto rebuilt=Build(body,profile,g,in,plan,keys,scan,budget,true,contract);
  if(const auto* f=std::get_if<CloseCompletionFailure>(&rebuilt))return *f;
  const auto& expected=std::get<CloseCompletionSummary>(rebuilt);auto work=expected.work;
  const auto step=[&](){if(work==budget.work)return false;++work;return true;};
  if(expected.method!=supplied.method || expected.revision!=supplied.revision || expected.cfg_revision!=supplied.cfg_revision ||
      expected.entries.size()!=supplied.entries.size() || expected.regions.size()!=supplied.regions.size())
    return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
  for(std::size_t i=0;i<expected.entries.size();++i) {
    if(!step())return CloseCompletionFailure{CloseCompletionError::kWorkLimit};
    if(expected.entries[i]!=supplied.entries[i])return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
  }
  for(std::size_t i=0;i<expected.regions.size();++i) {
    const auto& a=expected.regions[i];const auto& b=supplied.regions[i];
    if(!step())return CloseCompletionFailure{CloseCompletionError::kWorkLimit};
    if(a.protocol!=b.protocol || a.read!=b.read || a.call!=b.call || a.members.size()!=b.members.size() || a.edges.size()!=b.edges.size())
      return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
    if(a.boundary.has_value()!=b.boundary.has_value())return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
    if(a.boundary) {
      if(a.boundary->entry_coverage!=b.boundary->entry_coverage || a.boundary->port_coverage!=b.boundary->port_coverage ||
          a.boundary->private_state!=b.boundary->private_state || a.boundary->region_coverage!=b.boundary->region_coverage ||
          a.boundary->missing!=b.boundary->missing || a.boundary->ports.size()!=b.boundary->ports.size() ||
          a.boundary->unreachable_entries.size()!=b.boundary->unreachable_entries.size() ||
          a.boundary->private_slots.size()!=b.boundary->private_slots.size() || a.boundary->regions.size()!=b.boundary->regions.size())
        return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
      const auto equal=[&](const auto& x,const auto& y)->std::optional<CloseCompletionFailure> {
        for(std::size_t k=0;k<x.size();++k) {
          if(!step())return CloseCompletionFailure{CloseCompletionError::kWorkLimit};
          if(x[k]!=y[k])return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
        }
        return {};
      };
      if(const auto f=equal(a.boundary->ports,b.boundary->ports))return *f;
      if(const auto f=equal(a.boundary->unreachable_entries,b.boundary->unreachable_entries))return *f;
      if(const auto f=equal(a.boundary->private_slots,b.boundary->private_slots))return *f;
      if(const auto f=equal(a.boundary->regions,b.boundary->regions))return *f;
    }
    if(a.outcomes.has_value()!=b.outcomes.has_value())return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
    if(a.outcomes) {
      if(a.outcomes->arrivals.size()!=b.outcomes->arrivals.size() || a.outcomes->leaving.size()!=b.outcomes->leaving.size())
        return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
      if(a.outcomes->catch_payloads.size()!=b.outcomes->catch_payloads.size())return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
      for(std::size_t e=0;e<a.outcomes->catch_payloads.size();++e) {
        if(!step())return CloseCompletionFailure{CloseCompletionError::kWorkLimit};
        if(a.outcomes->catch_payloads[e]!=b.outcomes->catch_payloads[e])return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
      }
      if(a.outcomes->events.size()!=b.outcomes->events.size())return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
      for(std::size_t e=0;e<a.outcomes->events.size();++e) {
        if(!step())return CloseCompletionFailure{CloseCompletionError::kWorkLimit};
        if(a.outcomes->events[e]!=b.outcomes->events[e])return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
      }
      if(a.outcomes->comparisons.size()!=b.outcomes->comparisons.size())return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
      for(std::size_t e=0;e<a.outcomes->comparisons.size();++e) {
        if(!step())return CloseCompletionFailure{CloseCompletionError::kWorkLimit};
        if(a.outcomes->comparisons[e]!=b.outcomes->comparisons[e])return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
      }
      for(std::size_t e=0;e<a.outcomes->arrivals.size();++e) {
        if(!step())return CloseCompletionFailure{CloseCompletionError::kWorkLimit};
        if(a.outcomes->arrivals[e]!=b.outcomes->arrivals[e])return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
      }
      for(std::size_t e=0;e<a.outcomes->leaving.size();++e) {
        if(!step())return CloseCompletionFailure{CloseCompletionError::kWorkLimit};
        if(a.outcomes->leaving[e]!=b.outcomes->leaving[e])return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
      }
    }
    for(std::size_t m=0;m<a.members.size();++m) {
      if(!step())return CloseCompletionFailure{CloseCompletionError::kWorkLimit};
      if(a.members[m]!=b.members[m])return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
    }
    for(std::size_t e=0;e<a.edges.size();++e) {
      if(!step())return CloseCompletionFailure{CloseCompletionError::kWorkLimit};
      if(a.edges[e]!=b.edges[e])return CloseCompletionFailure{CloseCompletionError::kCorrespondence};
    }
  }
  return work;
}
}  // namespace abcd::source
