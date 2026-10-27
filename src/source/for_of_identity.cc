#include "for_of_identity.h"
#include "completion_ports.h"
#include "../analysis/idf_scratch.h"
#include "../analysis/value_phi.h"
#include "abcd/analysis/dominators.h"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
struct Quota {
  analysis::detail::IdfScratch memory;
  std::uint64_t left, initial;
  ForOfIdentityFailure witness{ForOfIdentityError::kCorrespondence};
  explicit Quota(SemanticBudget b):memory(b.bytes),left(b.work),initial(b.work) {}
  void Step(std::uint64_t n=1) {if(n>left)Fail(ForOfIdentityError::kWorkLimit);left-=n;}
  [[noreturn]] void Fail(ForOfIdentityError e) {witness.kind=e;throw witness;}
  void Require(bool ok,ForOfIdentityError e=ForOfIdentityError::kCorrespondence) {if(!ok)Fail(e);}
  SemanticBudget Remaining() const {return {memory.Available(),left};}
  template<class T> void Size(std::vector<T>& v,std::size_t n) {
    Require(n<=UINT32_MAX && n<=memory.Available()/sizeof(T),ForOfIdentityError::kResourceLimit);
    Step(n);memory.Retain(n*sizeof(T));v.resize(n);memory.Retain((v.capacity()-n)*sizeof(T));
  }
};
class Proof {
 public:
  Proof(const ValueProgram& graph,const PhasedValueInput& input,Quota& quota)
      :g(graph),in(input),q(quota),commits(&q.memory),keys(&q.memory),
       original_ops(&q.memory),instruction_ops(&q.memory),close(&q.memory),offsets(&q.memory),targets(&q.memory) {}
  void Index(const IteratorKeys& names,const CloseCompletionSummary& summary) {
    ports=summary.completion_ports.get();
    q.Require(g.phases.size()==g.blocks.size());
    q.Require(names.revision==g.revision && names.cfg_revision==g.cfg_revision);
    auto canonical=AnalyzeValueIdentities(g,q.Remaining());
    if(const auto* e=std::get_if<ValueSimplifyError>(&canonical))
      q.Fail(*e==ValueSimplifyError::kResourceLimit?ForOfIdentityError::kResourceLimit:
          *e==ValueSimplifyError::kWorkLimit?ForOfIdentityError::kWorkLimit:ForOfIdentityError::kIdentity);
    ids=std::get<ValueIdentities>(std::move(canonical));q.memory.Retain(ids.allocated_bytes);q.Step(ids.work);
    commits.resize(g.ops.size());keys.resize(g.ops.size());close.resize(g.ops.size());
    original_ops.resize(in.flow.nodes.size());instruction_ops.resize(in.flow.instructions.size());
    for(std::uint32_t o=0;o<g.ops.size();++o) {
      q.Step();const auto& op=g.ops[o];
      q.Require(op.block.value<g.phases.size());
      const auto n=g.phases[op.block.value].input;
      q.Require(n.value<original_ops.size());
      if(op.life==ValueLife::kLive){q.Require(!original_ops[n.value]);original_ops[n.value]=OpId{o};
        const auto site=g.origins[op.origin.value].site.value;q.Require(site<instruction_ops.size() && !instruction_ops[site]);instruction_ops[site]=OpId{o};}
    }
    for(std::uint32_t b=0;b<g.phases.size();++b) {
      q.Step();const auto& p=g.phases[b];
      if(p.phase==ExceptionPhase::kCommit && p.operation){q.Require(p.operation->value<commits.size() && !commits[p.operation->value]);commits[p.operation->value]=WorkBlockId{b};}
    }
    for(const auto& key:names.uses){q.Step();q.Require(key.operation.value<keys.size() && key.origin<names.origins.size());keys[key.operation.value]=names.origins[key.origin].key;}
    for(const auto& r:summary.regions)for(const auto& member:r.members) {
      q.Step();q.Require(member.node.value<original_ops.size());
      if(const auto op=original_ops[member.node.value]) {
        if(close[op->value] && close[op->value]!=r.protocol) {
          q.Step(2*(1+std::bit_width(ports?ports->members.size():0)));
          q.Require(ports && CompletionPortOwns(*ports,member.node,*close[op->value]) &&
              CompletionPortOwns(*ports,member.node,r.protocol));
          close[op->value]=std::min(*close[op->value],r.protocol);
        } else close[op->value]=r.protocol;
      }
    }
    offsets.resize(g.blocks.size()+1);
    targets.resize(g.edges.size());
    for(const auto& e:g.edges){q.Step();q.Require(e.source.value<g.blocks.size() && e.target.value<g.blocks.size());++offsets[e.source.value+1];}
    for(std::size_t i=1;i<offsets.size();++i){q.Step();offsets[i]+=offsets[i-1];}
    auto cursor=std::pmr::vector<std::uint32_t>(offsets,&q.memory);
    for(const auto& e:g.edges){q.Step();targets[cursor[e.source.value]++]={e.target.value};}
    auto dominated=BuildDominators({offsets,targets},{},{q.memory.Available(),q.left});
    if(const auto* e=std::get_if<DominatorError>(&dominated))q.Fail(*e==DominatorError::kResourceLimit?ForOfIdentityError::kResourceLimit:*e==DominatorError::kWorkLimit?ForOfIdentityError::kWorkLimit:ForOfIdentityError::kCorrespondence);
    dom=std::get<DominatorTree>(std::move(dominated));q.memory.Retain(dom.work.allocated_bytes);q.Step(dom.work.steps);
  }
  ValueId Canon(ValueId v) {q.Require(v.value<ids.canonical.size());return ids.canonical[v.value];}
  std::optional<ValueId> Operand(OpId o,OperandRole role) {
    const auto& op=g.ops[o.value];std::optional<ValueId> result;
    for(std::uint32_t u=0;u<op.use_count;++u){q.Step();const auto& use=g.uses[op.first_use.value+u];if(use.role!=role)continue;
      q.Require(!result);if(const auto* v=std::get_if<ValueId>(&use.operand))result=Canon(*v);
    }return result;
  }
  ValueId Result(OpId o) {q.witness.operation=o;q.Require(g.ops[o.value].result.has_value(),ForOfIdentityError::kIdentity);return Canon(*g.ops[o.value].result);}
  OpId Original(ExceptionNodeId n) {
    q.Require(n.value<in.flow.nodes.size());
    // Entry evidence may identify the Commit of a decision. Resolve by the
    // immutable instruction identity; never choose a neighbouring PC.
    const auto instruction=in.flow.nodes[n.value].instruction;
    q.Require(instruction.value<instruction_ops.size() && instruction_ops[instruction.value]);
    return *instruction_ops[instruction.value];
  }
  WorkBlockId Commit(OpId o) {q.witness.operation=o;q.Require(commits[o.value].has_value(),ForOfIdentityError::kExceptionCorrespondence);return *commits[o.value];}
  bool Dominates(WorkBlockId a,WorkBlockId b) {q.Step();return dom.Dominates({a.value},{b.value});}
  bool Before(OpId a,OpId b) {return Dominates(Commit(a),g.ops[b.value].block);}
  void Mark(ForOfIdentityIndex& plan,OpId o,IteratorProtocolId p,ForOfRole role) {
    auto& m=plan.operations[o.value];q.Require(!m || *m==ForOfMember{p,role},ForOfIdentityError::kIdentity);m=ForOfMember{p,role};
  }
  void Build(ForOfIdentityIndex& plan,const IteratorProtocolScan& scan,const CloseEntries& entries) {
    q.Size(plan.loops,scan.candidates.size());q.Size(plan.operations,g.ops.size());q.Size(plan.values,g.values.size());q.Size(plan.uses,g.uses.size());
    std::pmr::vector<std::optional<std::uint32_t>> nexts(g.values.size(),{},&q.memory),results(g.values.size(),{},&q.memory);
    std::pmr::vector<std::optional<OpId>> calls(scan.candidates.size(),{},&q.memory),checks(scan.candidates.size(),{},&q.memory),done(scan.candidates.size(),{},&q.memory);
    for(const auto& c:scan.candidates) {
      q.Step();q.witness.acquisition=c.acquisition.operation;
      q.Require(c.id.value<plan.loops.size() && c.next,ForOfIdentityError::kCachedNext);
      q.Require(Canon(c.iterator)==Result(c.acquisition.operation),ForOfIdentityError::kIdentity);
      q.Require(Operand(*c.next,OperandRole::kBase)==Canon(c.iterator) && Before(c.acquisition.operation,*c.next),ForOfIdentityError::kCachedNext);
      const auto n=Result(*c.next);q.Require(!nexts[n.value],ForOfIdentityError::kIdentity);nexts[n.value]=c.id.value;
    }
    for(std::uint32_t o=0;o<g.ops.size();++o) {
      q.Step();const auto& op=g.ops[o];if(op.life!=ValueLife::kLive || !std::holds_alternative<ValueInvoke>(op.operation))continue;
      const auto callee=Operand({o},OperandRole::kCallee);if(!callee || !nexts[callee->value])continue;
      const auto p=*nexts[callee->value];const auto& c=scan.candidates[p];q.witness.acquisition=c.acquisition.operation;q.witness.operation=OpId{o};
      q.Require(!calls[p],ForOfIdentityError::kAdvance);q.Require(std::get<ValueInvoke>(op.operation).arguments==0 &&
          Operand({o},OperandRole::kReceiver)==Canon(c.iterator),ForOfIdentityError::kIdentity);
      q.Require(Before(*c.next,{o}),ForOfIdentityError::kCachedNext);calls[p]=OpId{o};results[Result({o}).value]=p;
    }
    for(std::uint32_t o=0;o<g.ops.size();++o) {
      q.Step();const auto& op=g.ops[o];if(op.life!=ValueLife::kLive)continue;
      if(std::holds_alternative<ValueObjectCheck>(op.operation)) {
        const auto input=Operand({o},OperandRole::kGuardValue);if(input && results[input->value]) {
          const auto p=*results[input->value];q.Require(!checks[p],ForOfIdentityError::kObjectCheck);checks[p]=OpId{o};
        }
      }
      if(keys[o]==IteratorKey::kDone) {
        const auto base=Operand({o},OperandRole::kBase);if(base && results[base->value]) {
          const auto p=*results[base->value];q.Require(!done[p],ForOfIdentityError::kDone);done[p]=OpId{o};
        }
      }
    }
    std::pmr::vector<const DerivedClose*> by_protocol(scan.candidates.size(),nullptr,&q.memory);
    for(const auto& close:entries.closes) {
      q.Step();q.Require(close.protocol.value<by_protocol.size());auto& item=by_protocol[close.protocol.value];
      q.Require(!item || item->iteration==close.iteration);item=&close;
    }
    std::pmr::vector<std::optional<WorkBlockId>> phases(in.flow.nodes.size(),{},&q.memory);
    for(std::uint32_t b=0;b<g.phases.size();++b){q.Step();const auto n=g.phases[b].input;q.Require(n.value<phases.size() && !phases[n.value]);phases[n.value]=WorkBlockId{b};}
    for(const auto& c:scan.candidates) {
      q.Step();const auto p=c.id.value;q.witness.acquisition=c.acquisition.operation;
      q.Require(calls[p] && checks[p] && done[p],ForOfIdentityError::kObjectCheck);
      const auto* derived=by_protocol[p];
      q.Require(derived && derived->iteration.value,ForOfIdentityError::kValueOrder);
      const auto& b=derived->iteration;
      const auto advance=Original(b.advance),decision=Original(b.exhausted.source),value=Original(*b.value);
      q.Require(advance==*calls[p] && keys[value.value]==IteratorKey::kValue && Operand(value,OperandRole::kBase)==Result(advance),ForOfIdentityError::kIdentity);
      q.Require(Before(advance,*checks[p]) && Before(*checks[p],*done[p]),ForOfIdentityError::kObjectCheck);
      q.Require(Before(*done[p],decision) && Before(decision,value),ForOfIdentityError::kDone);
      q.Require(b.success.value<phases.size() && phases[b.success.value] &&
          Dominates(*phases[b.success.value],g.ops[value.value].block),ForOfIdentityError::kValueOrder);
      plan.loops[p]={c.id,c.acquisition.operation,*c.next,advance,*checks[p],*done[p],decision,value,
        Canon(c.iterator),Result(*c.next),Result(advance),Result(value),b.exhausted,b.success,b.loop_header};
      const auto& l=plan.loops[p];
      for(const auto [o,role]:{std::pair{l.acquisition,ForOfRole::kAcquisition},{l.next,ForOfRole::kCachedNext},{l.advance,ForOfRole::kAdvance},
          {l.check,ForOfRole::kObjectCheck},{l.done,ForOfRole::kDone},{l.decision,ForOfRole::kDecision},{l.value,ForOfRole::kValue}})Mark(plan,o,c.id,role);
      for(const auto [v,role]:{std::pair{l.iterator,ForOfRole::kAcquisition},{l.cached_next,ForOfRole::kCachedNext},{l.result,ForOfRole::kAdvance}}) {
        q.Require(!plan.values[v.value],ForOfIdentityError::kIdentity);plan.values[v.value]=ForOfMember{c.id,role};
      }
    }
    // Close membership was independently sealed by B. Body ops stay absent.
    for(std::uint32_t o=0;o<close.size();++o){q.Step();if(close[o])Mark(plan,{o},*close[o],ForOfRole::kClose);}
    for(std::uint32_t v=0;v<g.values.size();++v){q.Step();const auto canonical=Canon({v});if(plan.values[canonical.value])plan.values[v]=plan.values[canonical.value];}
    for(std::uint32_t o=0;o<g.ops.size();++o){q.Step();const auto& op=g.ops[o];if(op.life!=ValueLife::kLive)continue;
      if(!plan.operations[o] && std::holds_alternative<ValueCopy>(op.operation) && op.result && plan.values[op.result->value])Mark(plan,{o},plan.values[op.result->value]->protocol,ForOfRole::kTransport);
    }
    for(std::uint32_t u=0;u<g.uses.size();++u) {
      q.Step();const auto& use=g.uses[u];if(use.life!=ValueLife::kLive)continue;
      const auto* value=std::get_if<ValueId>(&use.operand);if(!value || !plan.values[value->value])continue;
      q.witness.use=UseId{u};q.witness.value=*value;const auto member=*plan.values[value->value];
      if(const auto* phi=std::get_if<ValueId>(&use.owner))q.Require(plan.values[phi->value]==member,ForOfIdentityError::kEscape);
      else {
        const auto owner=std::get<OpId>(use.owner);q.witness.operation=owner;
        bool supported=plan.operations[owner.value] && plan.operations[owner.value]->protocol==member.protocol;
        if(!supported && ports && plan.operations[owner.value] && plan.operations[owner.value]->role==ForOfRole::kClose) {
          q.Step(1+std::bit_width(ports->members.size()));
          supported=CompletionPortOwns(*ports,g.phases[g.ops[owner.value].block.value].input,member.protocol);
        }
        q.Require(supported,ForOfIdentityError::kEscape);
        const auto role=plan.operations[owner.value]->role;
        q.Require(role==ForOfRole::kTransport || role==ForOfRole::kClose ||
          (member.role==ForOfRole::kAcquisition && ((role==ForOfRole::kCachedNext && use.role==OperandRole::kBase)||(role==ForOfRole::kAdvance && use.role==OperandRole::kReceiver))) ||
          (member.role==ForOfRole::kCachedNext && role==ForOfRole::kAdvance && use.role==OperandRole::kCallee) ||
          (member.role==ForOfRole::kAdvance && ((role==ForOfRole::kObjectCheck && use.role==OperandRole::kGuardValue)||((role==ForOfRole::kDone || role==ForOfRole::kValue)&&use.role==OperandRole::kBase))),ForOfIdentityError::kEscape);
      }
      plan.uses[u]=member;
    }
  }
 private:
  const ValueProgram& g;const PhasedValueInput& in;Quota& q;
  const CompletionPorts* ports=nullptr;
  ValueIdentities ids;DominatorTree dom;
  std::pmr::vector<std::optional<WorkBlockId>> commits;
  std::pmr::vector<std::optional<IteratorKey>> keys;
  std::pmr::vector<std::optional<OpId>> original_ops, instruction_ops;
  std::pmr::vector<std::optional<IteratorProtocolId>> close;
  std::pmr::vector<std::uint32_t> offsets;
  std::pmr::vector<BlockId> targets;
};
}
std::variant<ForOfIdentityIndex,ForOfIdentityFailure> IndexForOfIdentities(const abc::MethodBody& body,const analysis::ValueProgram& graph,
    const analysis::PhasedValueInput& input,const ExceptionPlan& regions,const ExceptionControl& control,
    const VerifiedProtocolRegionOwnership& ownership,SourceContract contract,analysis::SemanticBudget budget) {
  try {
    Quota q(budget);
    if(IterationAcquisition(contract)!=IterationAcquisitionSemantics::kStandardV1)return ForOfIdentityFailure{ForOfIdentityError::kAcquisitionSemantics};
    q.Require(contract==ownership.Contract());
    auto checked=VerifyExceptionControl(body,graph,input,regions,control,q.Remaining(),&ownership);
    if(const auto* e=std::get_if<ExceptionControlError>(&checked))q.Fail(*e==ExceptionControlError::kResourceLimit?ForOfIdentityError::kResourceLimit:*e==ExceptionControlError::kWorkLimit?ForOfIdentityError::kWorkLimit:ForOfIdentityError::kCorrespondence);
    q.Step(std::get<std::uint64_t>(checked));
    auto derived=DeriveCloseEntries(body,ownership.Profile(),graph,input,regions,ownership.Keys(),ownership.Scan(),q.Remaining(),ownership.Summary().replay);
    if(const auto* e=std::get_if<CloseEntryFailure>(&derived))q.Fail(e->kind==CloseEntryError::kResourceLimit?ForOfIdentityError::kResourceLimit:e->kind==CloseEntryError::kWorkLimit?ForOfIdentityError::kWorkLimit:ForOfIdentityError::kCorrespondence);
    auto entries=std::get<CloseEntries>(std::move(derived));q.memory.Observe(entries.peak_bytes);q.Step(entries.work);
    // Delegated scratch is released before this call returns. Transfer every
    // surviving capacity, not the unused grant and not just vector heads.
    const auto retain=[&](const auto& values){q.memory.Retain(values.capacity()*sizeof(typename std::decay_t<decltype(values)>::value_type));};
    retain(entries.closes);
    for(const auto& c:entries.closes){q.Step();retain(c.entries);retain(c.exits);retain(c.relation.arrivals);retain(c.relation.leaving);}
    ForOfIdentityIndex result{graph.revision,graph.cfg_revision};
    Proof proof(graph,input,q);proof.Index(ownership.Keys(),ownership.Summary());proof.Build(result,ownership.Scan(),entries);
    result.allocated_bytes=result.loops.capacity()*sizeof(ForOfLoop)+
      (result.operations.capacity()+result.values.capacity()+result.uses.capacity())*sizeof(std::optional<ForOfMember>);
    result.peak_bytes=q.memory.Peak();result.work=q.initial-q.left;return result;
  } catch(const ForOfIdentityFailure& f){return f;}
    catch(const std::bad_alloc&){return ForOfIdentityFailure{ForOfIdentityError::kResourceLimit};}
    catch(const std::length_error&){return ForOfIdentityFailure{ForOfIdentityError::kResourceLimit};}
}
std::variant<std::uint64_t,ForOfIdentityFailure> VerifyForOfIdentityIndex(const abc::MethodBody& body,const analysis::ValueProgram& graph,
    const analysis::PhasedValueInput& input,const ExceptionPlan& regions,const ExceptionControl& control,
    const VerifiedProtocolRegionOwnership& owner,SourceContract contract,const ForOfIdentityIndex& plan,analysis::SemanticBudget budget) {
  auto built=IndexForOfIdentities(body,graph,input,regions,control,owner,contract,budget);
  if(const auto* f=std::get_if<ForOfIdentityFailure>(&built))return *f;
  const auto& expected=std::get<ForOfIdentityIndex>(built);
  const auto comparisons=std::uint64_t{expected.loops.size()}+expected.operations.size()+expected.values.size()+expected.uses.size()+1;
  if(comparisons>budget.work-expected.work)return ForOfIdentityFailure{ForOfIdentityError::kWorkLimit};
  if(plan.revision!=graph.revision || plan.cfg_revision!=graph.cfg_revision || plan.loops!=expected.loops ||
      plan.operations!=expected.operations || plan.values!=expected.values || plan.uses!=expected.uses)return ForOfIdentityFailure{ForOfIdentityError::kCorrespondence};
  return expected.work+comparisons;
}
}  // namespace abcd::source
