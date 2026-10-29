#include "for_of_evaluation.h"
#include "completion_ports.h"
#include "../analysis/idf_scratch.h"
#include "../analysis/value_edits.h"
#include <deque>
#include <stdexcept>
namespace abcd::source {
namespace {
using namespace analysis;
using Error=ForOfIterationError;
struct Quota {
  analysis::detail::IdfScratch memory;std::uint64_t initial,left;
  explicit Quota(SemanticBudget b):memory(b.bytes),initial(b.work),left(b.work){}
  void Step(std::uint64_t n=1){if(n>left)throw ForOfIterationFailure{Error::kWorkLimit};left-=n;}
  void Require(bool b){if(!b)throw ForOfIterationFailure{Error::kCorrespondence};}
  SemanticBudget Remaining()const{return {memory.Available(),left};}
  void Retain(std::size_t n){memory.Retain(n);}
  template<class T>void Array(std::vector<T>& v,std::size_t n){if(n>UINT32_MAX || n>memory.Available()/sizeof(T))throw ForOfIterationFailure{Error::kResourceLimit};Step(n);Retain(n*sizeof(T));v.resize(n);Retain((v.capacity()-n)*sizeof(T));}
};
// absorb_copies: without a port graph, also absorb plain register copies of
// private close state from the loop body. Optimized code spills that state
// into handler registers; it is tried only after the stricter plan refuses.
void Index(const ValueProgram& g,const ForOfEvaluationInput& source,ForOfEvaluationPlan& p,Quota& q,bool absorb_copies) {
  q.Array(p.operations,g.ops.size());q.Array(p.values,g.values.size());q.Array(p.uses,g.uses.size());
  std::pmr::vector<std::uint8_t> ordinary(g.values.size(),0,&q.memory);
  std::pmr::deque<ValueId> pending(&q.memory);
  std::pmr::vector<std::uint8_t> body_nodes(source.input.flow.nodes.size(),0,&q.memory);
  const auto* ports=source.ownership.Summary().completion_ports.get();
  bool inline_closes=ports!=nullptr;
  for(const auto& region:source.ownership.Summary().regions)for(const auto& range:region.boundary->protected_ranges){q.Step();if(range.role==ProtectedRangeRole::kLoopBody){inline_closes|=!range.protocol.empty();for(const auto node:range.ordinary){q.Step();q.Require(node.value<body_nodes.size());body_nodes[node.value]=1;}}}

  const auto keep=[&](ValueId v){q.Step();if(!ordinary[v.value]){ordinary[v.value]=1;pending.push_back(v);}};
  // Observable ordinary uses are roots; pure transport is followed backwards,
  // not promoted to an observation of dead VM state. The success value is an explicit protocol output,
  // not an internal iterator value, including its real body copies/phis.
  for(const auto& d:p.iteration.definitions){q.Step();keep(d.value);}
  if(ports)for(const auto& loop:p.identity.loops) {
    const auto& acquire=g.ops[loop.acquisition.value];
    for(std::uint32_t u=0;u<acquire.use_count;++u) {
      q.Step();if(const auto* value=std::get_if<ValueId>(&g.uses[acquire.first_use.value+u].operand))keep(*value);
    }
  }
  if(ports)for(const auto& entry:ports->entries) {
    q.Step();if(entry.original.pending.kind==PendingCompletionKind::kReturn)
      if(const auto* value=std::get_if<SsaValueId>(&entry.original.pending.payload)) {
        q.Require(value->value<g.input.values.size());keep(g.input.values[value->value]);
      }
  }
  for(const auto& use:g.uses){q.Step();if(use.life!=ValueLife::kLive)continue;const auto* v=std::get_if<ValueId>(&use.operand);if(!v)continue;
    if(const auto* o=std::get_if<OpId>(&use.owner);o && !p.identity.operations[o->value] &&
        (!inline_closes || !CanErasePureOp(g.ops[o->value])) &&
        (source.ownership.Forwarding().operations.empty() || !source.ownership.Forwarding().operations[o->value]))keep(*v);
  }
  if(ports) {
    std::pmr::vector<std::optional<UseId>> first_output(g.values.size(),{},&q.memory);
    std::pmr::vector<std::uint8_t> selected(g.values.size(),0,&q.memory);
    for(std::uint32_t u=0;u<g.uses.size();++u) {
      q.Step();const auto& use=g.uses[u];if(use.life==ValueLife::kLive)
        if(const auto* value=std::get_if<ValueId>(&use.operand);value && !first_output[value->value])first_output[value->value]=UseId{u};
    }
    q.memory.Retain(ports->entries.size()*sizeof(ForOfNativeOutput));p.outputs.reserve(ports->entries.size());q.memory.Retain((p.outputs.capacity()-ports->entries.size())*sizeof(ForOfNativeOutput));
    for(const auto& entry:ports->entries) {
      q.Step();if(entry.disposition!=CompletionPortDisposition::kAction || entry.original.pending.kind!=PendingCompletionKind::kReturn)continue;
      if(const auto* payload=std::get_if<SsaValueId>(&entry.original.pending.payload)) {
        const auto value=g.input.values[payload->value];if(selected[value.value])continue;selected[value.value]=1;
        q.Require(first_output[value.value].has_value());p.outputs.push_back({value,*first_output[value.value]});
      }
    }
  }
  // Backwards ordinary liveness stops at protocol events. These boundaries
  // deliver a value once; they never replay their iterator dependencies.
  std::pmr::vector<std::optional<UseId>> first(g.values.size(),{},&q.memory),next(g.uses.size(),{},&q.memory);
  for(std::uint32_t u=0;u<g.uses.size();++u){q.Step();const auto& use=g.uses[u];if(use.life==ValueLife::kLive)if(const auto* v=std::get_if<ValueId>(&use.owner)){next[u]=first[v->value];first[v->value]=UseId{u};}}
  while(!pending.empty()){q.Step();const auto v=pending.front();pending.pop_front();const auto& origin=g.values[v.value].origin;
    if(const auto* phi=std::get_if<PhiDefinition>(&origin)){(void)phi;for(auto u=first[v.value];u;u=next[u->value]){q.Step();if(const auto* input=std::get_if<ValueId>(&g.uses[u->value].operand))keep(*input);}}
    else if(const auto* def=std::get_if<OpResult>(&origin);def &&
        (!p.identity.operations[def->op.value] || (ports && CanErasePureOp(g.ops[def->op.value])))){const auto& o=g.ops[def->op.value];for(std::uint32_t n=0;n<o.use_count;++n){q.Step();if(const auto* input=std::get_if<ValueId>(&g.uses[o.first_use.value+n].operand))keep(*input);}}
  }
  for(std::size_t o=0;o<p.operations.size();++o){q.Step();p.operations[o]=p.identity.operations[o];}
  const auto& forwarding=source.ownership.Forwarding();
  if(!forwarding.operations.empty())for(std::uint32_t o=0;o<g.ops.size();++o){q.Step();if(forwarding.operations[o]){q.Require(!p.operations[o]);p.operations[o]=ForOfMember{IteratorProtocolId{0},ForOfRole::kForwarded};}}
  // Closed scalar dependencies of close code can be transported without a
  // source variable. Expand only erasure-safe pure definitions/phis; getters,
  // calls and all ordinary body events are never absorbed by this closure.
  std::pmr::deque<std::pair<ValueId,IteratorProtocolId>> queue(&q.memory);
  const auto claim=[&](ValueId v,IteratorProtocolId protocol){q.Step();if(ordinary[v.value])return;
    const auto& origin=g.values[v.value].origin;
    if(std::holds_alternative<EntryDefinition>(origin))return;
    if(const auto* def=std::get_if<OpResult>(&origin);def && !p.operations[def->op.value]) {
      const auto& op=g.ops[def->op.value];
      if(!CanErasePureOp(op) || (!ports && !(absorb_copies && std::holds_alternative<ValueCopy>(op.operation)) &&
          body_nodes[g.phases[op.block.value].input.value]))return;
    }
    // The label of a closed private value is a storage representative, not a
    // claim that two iterators/SSA values are equal. Only the complete port
    // graph permits shared dead transport; observable roots were kept above.
    if(p.values[v.value]){q.Require(p.values[v.value]->protocol==protocol ||
        (ports && p.values[v.value]->role==ForOfRole::kClose));return;}
    p.values[v.value]=ForOfMember{protocol,ForOfRole::kClose};queue.push_back({v,protocol});};
  for(std::uint32_t v=0;v<g.values.size();++v){q.Step();if(p.identity.values[v]){q.Require(!ordinary[v]);p.values[v]=p.identity.values[v];}}
  const auto inputs=[&](OpId id,IteratorProtocolId protocol){const auto& o=g.ops[id.value];for(std::uint32_t n=0;n<o.use_count;++n){q.Step();if(const auto* v=std::get_if<ValueId>(&g.uses[o.first_use.value+n].operand))claim(*v,protocol);}};
  for(std::uint32_t o=0;o<g.ops.size();++o){q.Step();if(p.operations[o]){
    if(g.ops[o].result)claim(*g.ops[o].result,p.operations[o]->protocol);
    if(p.operations[o]->role!=ForOfRole::kAcquisition)inputs({o},p.operations[o]->protocol);
  }}
  // A protocol handler may discard its pending exception outright (the
  // optimizer drops the dead catch store); that catch value is still private.
  for(const auto& r:source.ownership.Scan().regions) {
    q.Step();q.Require(r.region.value<source.regions.regions.size());
    if(const auto& b=source.regions.regions[r.region.value].binding)claim(b->value,r.protocol);
  }
  while(!queue.empty()){q.Step();const auto [v,protocol]=queue.front();queue.pop_front();const auto& origin=g.values[v.value].origin;
    // Close state can join after an abrupt copy without being read again.
    // Such a phi is private only when the shared backwards liveness proved
    // no ordinary observation. Claim the correlated phi and ALL its inputs;
    // never leave an edge copy from an internal guard into a source symbol.
    for(auto u=inline_closes?g.values[v.value].first_use:std::nullopt;u;u=g.uses[u->value].next) {
      q.Step();const auto& use=g.uses[u->value];
      if(use.life!=ValueLife::kLive)continue;
      if(const auto* phi=std::get_if<ValueId>(&use.owner))claim(*phi,protocol);
      else if(const auto* owner=std::get_if<OpId>(&use.owner);
          owner && !p.operations[owner->value] && CanErasePureOp(g.ops[owner->value]) &&
          (ports || (absorb_copies && std::holds_alternative<ValueCopy>(g.ops[owner->value].operation)))) {
        const auto& op=g.ops[owner->value];
        // No observable root can depend on this value: backwards liveness
        // would have reached it. Account for the complete dead pure closure,
        // including harmless body copies, instead of leaking private VM state.
        q.Require(!op.result || !ordinary[op.result->value]);
        p.operations[owner->value]=ForOfMember{protocol,ForOfRole::kTransport};
        inputs(*owner,protocol);if(op.result)claim(*op.result,protocol);
      }
    }
    if(const auto* def=std::get_if<OpResult>(&origin)){
      const auto& op=g.ops[def->op.value];if(!p.operations[def->op.value]){q.Require(CanErasePureOp(op));p.operations[def->op.value]=ForOfMember{protocol,ForOfRole::kTransport};inputs(def->op,protocol);}
    }else if(std::holds_alternative<PhiDefinition>(origin)){for(auto u=first[v.value];u;u=next[u->value]){q.Step();if(const auto* value=std::get_if<ValueId>(&g.uses[u->value].operand))claim(*value,protocol);}}
  }
  for(std::uint32_t u=0;u<g.uses.size();++u){q.Step();const auto& use=g.uses[u];if(use.life!=ValueLife::kLive)continue;const auto* v=std::get_if<ValueId>(&use.operand);
    if(const auto* target=std::get_if<ValueId>(&use.owner);target && p.values[target->value]) {
      const auto member=*p.values[target->value];
      if(v && p.values[v->value])q.Require(p.values[v->value]->protocol==member.protocol ||
          (ports && p.values[v->value]->role==ForOfRole::kClose && member.role==ForOfRole::kClose));
      p.uses[u].protocol=member.protocol;continue;
    }
    if(ports)if(const auto* owner=std::get_if<OpId>(&use.owner);
        owner && p.operations[owner->value] && p.operations[owner->value]->role==ForOfRole::kTransport) {
      // Dead transport consumes no source value, even when its input was
      // produced by an observable call. The call stays an ordinary root.
      p.uses[u].protocol=p.operations[owner->value]->protocol;continue;
    }
    if(!v || !p.values[v->value])continue;const auto member=*p.values[v->value];
    if(const auto* owner=std::get_if<OpId>(&use.owner)) { q.Require(p.operations[owner->value] &&
        (p.operations[owner->value]->protocol==member.protocol || (ports && member.role==ForOfRole::kClose &&
          (p.operations[owner->value]->role==ForOfRole::kClose || p.operations[owner->value]->role==ForOfRole::kTransport)))); }
    else {const auto target=std::get<ValueId>(use.owner);q.Require(p.values[target.value] &&
        (p.values[target.value]->protocol==member.protocol || (ports && member.role==ForOfRole::kClose && p.values[target.value]->role==ForOfRole::kClose)));}
    p.uses[u].protocol=member.protocol;
  }
}
}
std::variant<ForOfEvaluationPlan,ForOfIterationFailure> BuildForOfEvaluationPlan(
    const ValueProgram& g,const ForOfEvaluationInput& i,SemanticBudget budget) {
  try{
    Quota q(budget);ForOfEvaluationPlan p;p.completion_ports=i.ownership.Summary().completion_ports;
    auto x=IndexForOfIdentities(i.body,g,i.input,i.regions,i.control,i.ownership,i.contract,q.Remaining());
    if(const auto* f=std::get_if<ForOfIdentityFailure>(&x))return ForOfIterationFailure{f->kind==ForOfIdentityError::kResourceLimit?Error::kResourceLimit:f->kind==ForOfIdentityError::kWorkLimit?Error::kWorkLimit:f->kind==ForOfIdentityError::kAcquisitionSemantics?Error::kAcquisitionSemantics:Error::kIdentity,{},f->operation};
    p.identity=std::get<ForOfIdentityIndex>(std::move(x));q.Retain(p.identity.allocated_bytes);q.Step(p.identity.work);
    auto y=ProveForOfIterations(i.body,g,i.input,i.regions,i.control,i.ownership,i.contract,p.identity,q.Remaining());if(const auto* f=std::get_if<ForOfIterationFailure>(&y))return *f;
    p.iteration=std::get<ForOfIterationProof>(std::move(y));q.Retain(p.iteration.allocated_bytes);q.Step(p.iteration.work);
    auto z=BuildForOfEvents(i.body,g,i.input,i.regions,i.control,i.ownership,i.contract,p.identity,p.iteration,q.Remaining());if(const auto* f=std::get_if<ForOfIterationFailure>(&z))return *f;
    p.events=std::get<ForOfEvents>(std::move(z));q.Retain(p.events.allocated_bytes);q.Step(p.events.work);
    try{Index(g,i,p,q,false);}
    catch(const ForOfIterationFailure& f) {
      if(f.kind!=Error::kCorrespondence || p.completion_ports)throw;
      p.operations.clear();p.values.clear();p.uses.clear();p.outputs.clear();
      Index(g,i,p,q,true);
    }
    q.Array(p.bindings,g.values.size());
    for(const auto& definition:p.iteration.definitions) {
      q.Step();q.Require(definition.value.value<p.bindings.size());
      auto& owner=p.bindings[definition.value.value];
      q.Require(!owner || *owner==definition.protocol);owner=definition.protocol;
    }
    p.allocated_bytes=p.identity.allocated_bytes+p.iteration.allocated_bytes+p.events.allocated_bytes+(p.operations.capacity()+p.values.capacity())*sizeof(std::optional<ForOfMember>)+p.uses.capacity()*sizeof(ForOfUseDisposition)+p.bindings.capacity()*sizeof(std::optional<IteratorProtocolId>)+p.outputs.capacity()*sizeof(ForOfNativeOutput);p.work=q.initial-q.left;return p;
  }catch(const ForOfIterationFailure& f){return f;}catch(const std::bad_alloc&){return ForOfIterationFailure{Error::kResourceLimit};}catch(const std::length_error&){return ForOfIterationFailure{Error::kResourceLimit};}
}
std::variant<std::uint64_t,ForOfIterationFailure> VerifyForOfEvaluationPlan(
    const ValueProgram& g,const ForOfEvaluationInput& i,const ForOfEvaluationPlan& p,SemanticBudget budget){
  auto built=BuildForOfEvaluationPlan(g,i,budget);if(const auto* f=std::get_if<ForOfIterationFailure>(&built))return *f;const auto& e=std::get<ForOfEvaluationPlan>(built);
  const auto cost=e.outputs.size()+e.operations.size()+e.values.size()+e.uses.size()+e.bindings.size()+e.events.events.size()+e.events.commits.size()+e.events.ports.size()+e.identity.loops.size()+e.iteration.definitions.size()+e.iteration.phis.size()+e.iteration.references.size()+e.iteration.iterations.size()+e.identity.operations.size()+e.identity.values.size()+e.identity.uses.size()+1;
  if(cost>budget.work-e.work)return ForOfIterationFailure{Error::kWorkLimit};
  if(p.completion_ports!=e.completion_ports || p.outputs!=e.outputs || p.bindings!=e.bindings || p.operations!=e.operations || p.values!=e.values || p.uses!=e.uses || p.identity.loops!=e.identity.loops || p.identity.operations!=e.identity.operations || p.identity.values!=e.identity.values || p.identity.uses!=e.identity.uses || p.identity.revision!=g.revision || p.identity.cfg_revision!=g.cfg_revision || p.iteration.revision!=g.revision || p.iteration.cfg_revision!=g.cfg_revision || p.iteration.iterations!=e.iteration.iterations || p.iteration.definitions!=e.iteration.definitions || p.iteration.phis!=e.iteration.phis || p.iteration.references!=e.iteration.references || p.events.revision!=g.revision || p.events.cfg_revision!=g.cfg_revision || p.events.events!=e.events.events || p.events.commits!=e.events.commits || p.events.ports!=e.events.ports)return ForOfIterationFailure{Error::kCorrespondence};
  return e.work+cost;
}
EvaluationFailure ForOfEvaluationFailure(const ValueProgram& g,ForOfIterationFailure f,const CompletionPorts* ports){EvaluationFailure e{f.kind==Error::kResourceLimit?EvaluationError::kResourceLimit:f.kind==Error::kWorkLimit?EvaluationError::kWorkLimit:EvaluationError::kProtocolOperand};e.evaluation.for_of=f.kind;if(ports)e.evaluation.protocol_region=ports->activation;if(f.operation)e.evaluation.location=OperationLocation(g,*f.operation);return e;}
std::optional<EvaluationFailure> BuildForOfEvaluation(const ValueProgram& g,const ForOfEvaluationInput* i,EvaluationPlan& plan,SemanticBudget& left){
  if(!i)return {};auto built=BuildForOfEvaluationPlan(g,*i,left);if(const auto* f=std::get_if<ForOfIterationFailure>(&built)){auto failure=ForOfEvaluationFailure(g,*f);if(i->ownership.Summary().completion_ports)failure.evaluation.protocol_region=i->ownership.Summary().completion_ports->activation;return failure;}
  plan.for_of=std::get<ForOfEvaluationPlan>(std::move(built));left.bytes-=plan.for_of->allocated_bytes;left.work-=plan.for_of->work;return {};
}
std::variant<std::uint64_t,EvaluationFailure> VerifyForOfEvaluation(const ValueProgram& g,const ForOfEvaluationInput* i,const EvaluationPlan& p,SemanticBudget budget){
  if (bool(i) != p.for_of.has_value()) {
    EvaluationEvidence evidence;
    evidence.obligation = EvaluationObligation::kProtocolContext;
    evidence.protocol_context = i ? ProtocolContextCheck::kMissingPlan : ProtocolContextCheck::kMissingInput;
    // The mismatch owns the protocol acquisition, not an invented operand use.
    // Map its real origin even when a caller omitted the immutable proof input.
    for (std::uint32_t op = 0; op < g.ops.size(); ++op) {
      if (op >= budget.work) return EvaluationError::kWorkLimit;
      if (g.ops[op].life == ValueLife::kLive &&
          std::holds_alternative<ValueIteratorAcquire>(g.ops[op].operation)) {
        evidence.location = OperationLocation(g, OpId{op});
        evidence.value = g.ops[op].result;
        break;
      }
    }
    return EvaluationFailure{EvaluationError::kOwnership, evidence};
  }if(!i)return std::uint64_t{0};auto checked=VerifyForOfEvaluationPlan(g,*i,*p.for_of,budget);if(const auto* f=std::get_if<ForOfIterationFailure>(&checked))return ForOfEvaluationFailure(g,*f);return std::get<std::uint64_t>(checked);
}
}
