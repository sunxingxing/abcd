#include "for_of_evaluation.h"
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
void Index(const ValueProgram& g,const ForOfEvaluationInput& source,ForOfEvaluationPlan& p,Quota& q) {
  q.Array(p.operations,g.ops.size());q.Array(p.values,g.values.size());q.Array(p.uses,g.uses.size());
  std::pmr::vector<std::uint8_t> ordinary(g.values.size(),0,&q.memory);
  std::pmr::deque<ValueId> pending(&q.memory);
  std::pmr::vector<std::uint8_t> body_nodes(source.input.flow.nodes.size(),0,&q.memory);
  for(const auto& region:source.ownership.Summary().regions)for(const auto& range:region.boundary->protected_ranges){q.Step();if(range.role==ProtectedRangeRole::kLoopBody)for(const auto node:range.ordinary){q.Step();q.Require(node.value<body_nodes.size());body_nodes[node.value]=1;}}

  const auto keep=[&](ValueId v){q.Step();if(!ordinary[v.value]){ordinary[v.value]=1;pending.push_back(v);}};
  // Ordinary uses are roots. The success value is an explicit protocol output,
  // not an internal iterator value, including its real body copies/phis.
  for(const auto& d:p.iteration.definitions){q.Step();keep(d.value);}
  for(const auto& use:g.uses){q.Step();if(use.life!=ValueLife::kLive)continue;const auto* v=std::get_if<ValueId>(&use.operand);if(!v)continue;
    if(const auto* o=std::get_if<OpId>(&use.owner);o && !p.identity.operations[o->value])keep(*v);
  }
  // Backwards ordinary liveness stops at protocol events. These boundaries
  // deliver a value once; they never replay their iterator dependencies.
  std::pmr::vector<std::optional<UseId>> first(g.values.size(),{},&q.memory),next(g.uses.size(),{},&q.memory);
  for(std::uint32_t u=0;u<g.uses.size();++u){q.Step();const auto& use=g.uses[u];if(use.life==ValueLife::kLive)if(const auto* v=std::get_if<ValueId>(&use.owner)){next[u]=first[v->value];first[v->value]=UseId{u};}}
  while(!pending.empty()){q.Step();const auto v=pending.front();pending.pop_front();const auto& origin=g.values[v.value].origin;
    if(const auto* phi=std::get_if<PhiDefinition>(&origin)){(void)phi;for(auto u=first[v.value];u;u=next[u->value]){q.Step();if(const auto* input=std::get_if<ValueId>(&g.uses[u->value].operand))keep(*input);}}
    else if(const auto* def=std::get_if<OpResult>(&origin);def && !p.identity.operations[def->op.value]){const auto& o=g.ops[def->op.value];for(std::uint32_t n=0;n<o.use_count;++n){q.Step();if(const auto* input=std::get_if<ValueId>(&g.uses[o.first_use.value+n].operand))keep(*input);}}
  }
  for(std::size_t o=0;o<p.operations.size();++o){q.Step();p.operations[o]=p.identity.operations[o];}
  // Closed scalar dependencies of close code can be transported without a
  // source variable. Expand only erasure-safe pure definitions/phis; getters,
  // calls and all ordinary body events are never absorbed by this closure.
  std::pmr::deque<std::pair<ValueId,IteratorProtocolId>> queue(&q.memory);
  const auto claim=[&](ValueId v,IteratorProtocolId protocol){q.Step();if(ordinary[v.value])return;
    const auto& origin=g.values[v.value].origin;
    if(std::holds_alternative<EntryDefinition>(origin))return;
    if(const auto* def=std::get_if<OpResult>(&origin);def && !p.operations[def->op.value]) {
      const auto& op=g.ops[def->op.value];
      if(!CanErasePureOp(op) || body_nodes[g.phases[op.block.value].input.value])return;
    }
    if(p.values[v.value]){q.Require(p.values[v.value]->protocol==protocol);return;}p.values[v.value]=ForOfMember{protocol,ForOfRole::kClose};queue.push_back({v,protocol});};
  for(std::uint32_t v=0;v<g.values.size();++v){q.Step();if(p.identity.values[v]){q.Require(!ordinary[v]);p.values[v]=p.identity.values[v];}}
  const auto inputs=[&](OpId id,IteratorProtocolId protocol){const auto& o=g.ops[id.value];for(std::uint32_t n=0;n<o.use_count;++n){q.Step();if(const auto* v=std::get_if<ValueId>(&g.uses[o.first_use.value+n].operand))claim(*v,protocol);}};
  for(std::uint32_t o=0;o<g.ops.size();++o){q.Step();if(p.operations[o]){
    if(g.ops[o].result)claim(*g.ops[o].result,p.operations[o]->protocol);
    if(p.operations[o]->role!=ForOfRole::kAcquisition)inputs({o},p.operations[o]->protocol);
  }}
  while(!queue.empty()){q.Step();const auto [v,protocol]=queue.front();queue.pop_front();const auto& origin=g.values[v.value].origin;
    if(const auto* def=std::get_if<OpResult>(&origin)){
      const auto& op=g.ops[def->op.value];if(!p.operations[def->op.value]){q.Require(CanErasePureOp(op));p.operations[def->op.value]=ForOfMember{protocol,ForOfRole::kTransport};inputs(def->op,protocol);}
    }else if(std::holds_alternative<PhiDefinition>(origin)){for(auto u=first[v.value];u;u=next[u->value]){q.Step();if(const auto* value=std::get_if<ValueId>(&g.uses[u->value].operand))claim(*value,protocol);}}
  }
  for(std::uint32_t u=0;u<g.uses.size();++u){q.Step();const auto& use=g.uses[u];if(use.life!=ValueLife::kLive)continue;const auto* v=std::get_if<ValueId>(&use.operand);
    if(const auto* target=std::get_if<ValueId>(&use.owner);target && p.values[target->value]) {
      const auto member=*p.values[target->value];
      if(v && p.values[v->value])q.Require(p.values[v->value]->protocol==member.protocol);
      p.uses[u].protocol=member.protocol;continue;
    }
    if(!v || !p.values[v->value])continue;const auto member=*p.values[v->value];
    if(const auto* owner=std::get_if<OpId>(&use.owner))q.Require(p.operations[owner->value] && p.operations[owner->value]->protocol==member.protocol);
    else {const auto target=std::get<ValueId>(use.owner);q.Require(p.values[target.value] && p.values[target.value]->protocol==member.protocol);}
    p.uses[u].protocol=member.protocol;
  }
}
}
std::variant<ForOfEvaluationPlan,ForOfIterationFailure> BuildForOfEvaluationPlan(
    const ValueProgram& g,const ForOfEvaluationInput& i,SemanticBudget budget) {
  try{
    Quota q(budget);ForOfEvaluationPlan p;
    auto x=IndexForOfIdentities(i.body,g,i.input,i.regions,i.control,i.ownership,i.contract,q.Remaining());
    if(const auto* f=std::get_if<ForOfIdentityFailure>(&x))return ForOfIterationFailure{f->kind==ForOfIdentityError::kResourceLimit?Error::kResourceLimit:f->kind==ForOfIdentityError::kWorkLimit?Error::kWorkLimit:f->kind==ForOfIdentityError::kAcquisitionSemantics?Error::kAcquisitionSemantics:Error::kIdentity,{},f->operation};
    p.identity=std::get<ForOfIdentityIndex>(std::move(x));q.Retain(p.identity.allocated_bytes);q.Step(p.identity.work);
    auto y=ProveForOfIterations(i.body,g,i.input,i.regions,i.control,i.ownership,i.contract,p.identity,q.Remaining());if(const auto* f=std::get_if<ForOfIterationFailure>(&y))return *f;
    p.iteration=std::get<ForOfIterationProof>(std::move(y));q.Retain(p.iteration.allocated_bytes);q.Step(p.iteration.work);
    auto z=BuildForOfEvents(i.body,g,i.input,i.regions,i.control,i.ownership,i.contract,p.identity,p.iteration,q.Remaining());if(const auto* f=std::get_if<ForOfIterationFailure>(&z))return *f;
    p.events=std::get<ForOfEvents>(std::move(z));q.Retain(p.events.allocated_bytes);q.Step(p.events.work);
    Index(g,i,p,q);p.allocated_bytes=p.identity.allocated_bytes+p.iteration.allocated_bytes+p.events.allocated_bytes+(p.operations.capacity()+p.values.capacity())*sizeof(std::optional<ForOfMember>)+p.uses.capacity()*sizeof(ForOfUseDisposition);p.work=q.initial-q.left;return p;
  }catch(const ForOfIterationFailure& f){return f;}catch(const std::bad_alloc&){return ForOfIterationFailure{Error::kResourceLimit};}catch(const std::length_error&){return ForOfIterationFailure{Error::kResourceLimit};}
}
std::variant<std::uint64_t,ForOfIterationFailure> VerifyForOfEvaluationPlan(
    const ValueProgram& g,const ForOfEvaluationInput& i,const ForOfEvaluationPlan& p,SemanticBudget budget){
  auto built=BuildForOfEvaluationPlan(g,i,budget);if(const auto* f=std::get_if<ForOfIterationFailure>(&built))return *f;const auto& e=std::get<ForOfEvaluationPlan>(built);
  const auto cost=e.operations.size()+e.values.size()+e.uses.size()+e.events.events.size()+e.events.commits.size()+e.events.ports.size()+e.identity.loops.size()+e.iteration.definitions.size()+e.iteration.phis.size()+e.iteration.references.size()+e.iteration.iterations.size()+e.identity.operations.size()+e.identity.values.size()+e.identity.uses.size()+1;
  if(cost>budget.work-e.work)return ForOfIterationFailure{Error::kWorkLimit};
  if(p.operations!=e.operations || p.values!=e.values || p.uses!=e.uses || p.identity.loops!=e.identity.loops || p.identity.operations!=e.identity.operations || p.identity.values!=e.identity.values || p.identity.uses!=e.identity.uses || p.identity.revision!=g.revision || p.identity.cfg_revision!=g.cfg_revision || p.iteration.revision!=g.revision || p.iteration.cfg_revision!=g.cfg_revision || p.iteration.iterations!=e.iteration.iterations || p.iteration.definitions!=e.iteration.definitions || p.iteration.phis!=e.iteration.phis || p.iteration.references!=e.iteration.references || p.events.revision!=g.revision || p.events.cfg_revision!=g.cfg_revision || p.events.events!=e.events.events || p.events.commits!=e.events.commits || p.events.ports!=e.events.ports)return ForOfIterationFailure{Error::kCorrespondence};
  return e.work+cost;
}
EvaluationFailure ForOfEvaluationFailure(const ValueProgram& g,ForOfIterationFailure f){EvaluationFailure e{f.kind==Error::kResourceLimit?EvaluationError::kResourceLimit:f.kind==Error::kWorkLimit?EvaluationError::kWorkLimit:EvaluationError::kProtocolOperand};if(f.operation)e.evaluation.location=OperationLocation(g,*f.operation);return e;}
std::optional<EvaluationFailure> BuildForOfEvaluation(const ValueProgram& g,const ForOfEvaluationInput* i,EvaluationPlan& plan,SemanticBudget& left){
  if(!i)return {};auto built=BuildForOfEvaluationPlan(g,*i,left);if(const auto* f=std::get_if<ForOfIterationFailure>(&built))return ForOfEvaluationFailure(g,*f);
  plan.for_of=std::get<ForOfEvaluationPlan>(std::move(built));left.bytes-=plan.for_of->allocated_bytes;left.work-=plan.for_of->work;return {};
}
std::variant<std::uint64_t,EvaluationFailure> VerifyForOfEvaluation(const ValueProgram& g,const ForOfEvaluationInput* i,const EvaluationPlan& p,SemanticBudget budget){
  if(bool(i)!=p.for_of.has_value())return EvaluationError::kOwnership;if(!i)return std::uint64_t{0};auto checked=VerifyForOfEvaluationPlan(g,*i,*p.for_of,budget);if(const auto* f=std::get_if<ForOfIterationFailure>(&checked))return ForOfEvaluationFailure(g,*f);return std::get<std::uint64_t>(checked);
}
}
