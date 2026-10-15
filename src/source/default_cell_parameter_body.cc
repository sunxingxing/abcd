#include "default_body.h"
#include "../analysis/value_flow.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
using namespace analysis;
using Error=DefaultBodyError;
void Require(bool ok,Error e=Error::kProof){if(!ok)throw e;}
struct Budget {
  SemanticBudget initial,left;
  explicit Budget(SemanticBudget b):initial(b),left(b){}
  void Step(std::uint64_t n=1){Require(n<=left.work,Error::kWorkLimit);left.work-=n;}
  void Admit(std::size_t n,std::size_t w){Require(n<=left.bytes/w,Error::kResourceLimit);left.bytes-=n*w;}
  template<class T> void Reserve(std::vector<T>& v,std::size_t n){Admit(n,sizeof(T));v.reserve(n);Admit(v.capacity()-n,sizeof(T));}
  template<class T> void Size(std::vector<T>& v,std::size_t n){Reserve(v,n);Step(n);v.resize(n);}
  template<class T> void Clone(std::vector<T>& v,const std::vector<T>& a){Reserve(v,a.size());Step(a.size());v.assign(a.begin(),a.end());}
  void Cost(CellParameterDefaultCost c){Admit(c.allocated_bytes,1);Step(c.work);}
};
ValueProgram Clone(const ValueProgram& g,Budget& q){
  Require(g.phases.empty(),Error::kShape);ValueProgram b;b.arguments=g.arguments;b.revision=g.revision;b.cfg_revision=g.cfg_revision;
  q.Clone(b.blocks,g.blocks);q.Clone(b.edges,g.edges);q.Clone(b.ops,g.ops);q.Clone(b.values,g.values);q.Clone(b.uses,g.uses);q.Clone(b.origins,g.origins);
  q.Clone(b.exceptional_exits,g.exceptional_exits);q.Clone(b.input.sites,g.input.sites);q.Clone(b.input.values,g.input.values);return b;
}
void CheckFailure(const CellParameterDefaultFailure& f){
  if(f.kind==CellParameterDefaultError::kResourceLimit)throw Error::kResourceLimit;
  if(f.kind==CellParameterDefaultError::kWorkLimit)throw Error::kWorkLimit;
  throw Error::kProof;
}
// IDs of operations/values/origins remain stable. Unreachable pure arm blocks
// become tombstones in the preceding block's arena; they acquire no execution.
// A value escaping a deleted arm is a proof failure, never a discarded phi.
ValueProgram Residual(const ValueProgram& g,const CellParameterDefaultProof& proof,
    std::vector<DefaultOpPlacement>& placements,Budget& q){
  std::vector<std::optional<std::uint32_t>> moved;std::vector<std::optional<EdgeId>> selected;
  std::vector<OpId> queue;std::vector<std::uint8_t> reachable;std::vector<WorkBlockId> blocks,work;
  std::vector<std::optional<EdgeId>> edges;std::vector<std::optional<UseId>> last;
  q.Size(moved,g.ops.size());q.Size(selected,g.blocks.size());q.Reserve(queue,g.ops.size());
  const auto mark=[&](OpId op,std::uint32_t p){Require(op.value<moved.size());if(!moved[op.value]){moved[op.value]=p;queue.push_back(op);}else Require(*moved[op.value]==p);};
  for(const auto& d:proof.Defaults()){
    q.Step();for(auto op:{d.test,d.comparison,d.branch,d.read,d.guard,d.fallback_store})mark(op,d.parameter);
    selected[g.ops[d.branch.value].block.value]=d.missing;
  }
  for(std::size_t i=0;i<queue.size();++i){q.Step();const auto id=queue[i];const auto& op=g.ops[id.value];if(!op.result)continue;
    auto use=g.values[op.result->value].first_use;
    while(use){q.Step();const auto& u=g.uses[use->value];const auto* owner=std::get_if<OpId>(&u.owner);Require(owner);
      if(!moved[owner->value]){Require(std::holds_alternative<ValueCopy>(g.ops[owner->value].operation));mark(*owner,*moved[id.value]);}use=u.next;}
  }
  q.Size(reachable,g.blocks.size());q.Size(blocks,g.blocks.size());q.Reserve(work,g.blocks.size());q.Size(edges,g.edges.size());
  reachable[0]=1;work.push_back({0});
  for(std::size_t i=0;i<work.size();++i){q.Step();const auto id=work[i];const auto& b=g.blocks[id.value];
    for(std::uint32_t j=0;j<b.edge_count;++j){q.Step();const EdgeId edge{b.first_edge.value+j};if(selected[id.value]&&*selected[id.value]!=edge)continue;
      const auto target=g.edges[edge.value].target;if(!reachable[target.value]){reachable[target.value]=1;work.push_back(target);}}
  }
  auto body=Clone(g,q);body.blocks.clear();body.edges.clear();body.exceptional_exits.clear();
  for(std::uint32_t i=0;i<g.blocks.size();++i){q.Step();const auto& b=g.blocks[i];
    if(reachable[i]){blocks[i]={static_cast<std::uint32_t>(body.blocks.size())};body.blocks.push_back(b);body.blocks.back().first_edge={static_cast<std::uint32_t>(body.edges.size())};body.blocks.back().edge_count=0;
      for(std::uint32_t j=0;j<b.edge_count;++j){q.Step();const EdgeId id{b.first_edge.value+j};if(selected[i]&&*selected[i]!=id)continue;
        edges[id.value]=EdgeId{static_cast<std::uint32_t>(body.edges.size())};auto e=g.edges[id.value];e.source=blocks[i];e.successor=body.blocks.back().edge_count++;if(selected[i])e.kind=FlowEdgeKind::kFallthrough;body.edges.push_back(e);}
    }else{Require(!body.blocks.empty(),Error::kFlow);blocks[i]={static_cast<std::uint32_t>(body.blocks.size()-1)};body.blocks.back().op_count+=b.op_count;}
  }
  for(auto& e:body.edges){q.Step();e.target=blocks[e.target.value];}
  q.Reserve(placements,g.ops.size());
  for(std::uint32_t i=0;i<g.ops.size();++i){q.Step();auto& op=body.ops[i];const bool dead=!reachable[op.block.value];
    op.block=blocks[op.block.value];op.order=i-body.blocks[op.block.value].first_op.value;
    if(dead&&!moved[i]){Require(op.normal==LanguageEffect::kNone&&op.before_throw==LanguageEffect::kNone&&op.exceptional==ExceptionalCompletion::kImpossible,Error::kFlow);
      body.origins[op.origin.value].disposition=OriginDisposition::kElidedPure;}
    if(moved[i])placements.push_back({{i},*moved[i]});
    if(dead||moved[i]){op.life=ValueLife::kRemoved;if(op.result)body.values[op.result->value].life=ValueLife::kRemoved;
      op.operation=ValueNop{};op.normal=LanguageEffect::kNone;op.before_throw=LanguageEffect::kNone;op.exceptional=ExceptionalCompletion::kImpossible;op.exception_cause=ExceptionCause::kUnclassified;}
  }
  for(auto& v:body.values){q.Step();v.first_use.reset();if(auto* p=std::get_if<PhiDefinition>(&v.origin)){if(!reachable[p->block.value])v.life=ValueLife::kRemoved;p->block=blocks[p->block.value];}}
  q.Size(last,body.values.size());
  for(std::uint32_t i=0;i<body.uses.size();++i){q.Step();auto& u=body.uses[i];u.previous.reset();u.next.reset();
    const bool gone=std::holds_alternative<OpId>(u.owner)?body.ops[std::get<OpId>(u.owner).value].life!=ValueLife::kLive:body.values[std::get<ValueId>(u.owner).value].life!=ValueLife::kLive;
    if(gone)u.life=ValueLife::kRemoved;if(u.life!=ValueLife::kLive)continue;
    if(u.incoming)if(auto* e=std::get_if<EdgeId>(&*u.incoming)){Require(edges[e->value].has_value(),Error::kFlow);*e=*edges[e->value];}
    if(const auto* v=std::get_if<ValueId>(&u.operand)){Require(body.values[v->value].life==ValueLife::kLive,Error::kFlow);u.previous=last[v->value];if(u.previous)body.uses[u.previous->value].next=UseId{i};else body.values[v->value].first_use=UseId{i};last[v->value]=UseId{i};}
  }
  for(const auto& e:g.exceptional_exits){q.Step();if(body.ops[e.op.value].life==ValueLife::kLive)body.exceptional_exits.push_back(e);}
  for(auto& site:body.input.sites){q.Step();if(site&&body.ops[site->value].life!=ValueLife::kLive)site.reset();}
  body.input.values.clear();Require(g.revision.value<UINT64_MAX&&g.cfg_revision.value<UINT64_MAX,Error::kRevision);++body.revision.value;++body.cfg_revision.value;
  auto flow=VerifyValueFlow(body,q.left);if(const auto* p=std::get_if<ValueFlowProof>(&flow)){q.Admit(p->allocated_bytes,1);q.Step(p->work);}else {const auto error=std::get<ValueAvailabilityError>(flow);throw error==ValueAvailabilityError::kResourceLimit?Error::kResourceLimit:error==ValueAvailabilityError::kWorkLimit?Error::kWorkLimit:Error::kFlow;}
  return body;
}
}
std::variant<DefaultBody,DefaultBodyError> DefaultBody::BuildCellParameter(const analysis::ValueProgram& g,
    const analysis::ValueEnvironments& env,const CellPlan& cells,const ParameterPlan& parameters,
    std::uint32_t length,analysis::SemanticBudget budget){
  try{
    Budget q(budget);auto result=CellParameterDefaultProof::Build(g,env,cells,parameters,length,q.left);
    if(const auto* e=std::get_if<CellParameterDefaultFailure>(&result))CheckFailure(*e);
    auto proof=std::get<CellParameterDefaultProof>(std::move(result));q.Cost(proof.Cost());
    const auto independent=VerifyCellParameterDefaults(g,env,cells,parameters,length,proof.Defaults(),q.left);
    if(const auto* e=std::get_if<CellParameterDefaultFailure>(&independent))CheckFailure(*e);q.Cost(std::get<CellParameterDefaultCost>(independent));
    q.Admit(1,sizeof(DefaultBody));q.Admit(1,sizeof(ValueProgram));
    auto original=std::make_unique<ValueProgram>(Clone(g,q));std::vector<DefaultOpPlacement> operations;
    auto residual=Residual(g,proof,operations,q);
    std::vector<DefaultInitializer> initializers;std::vector<SignatureCapture> captures;std::vector<InitializedParameterCommit> commits;std::vector<std::uint32_t> sites;
    std::vector<ValueExceptionalExit> exceptions;std::vector<std::optional<std::uint32_t>> exits;
    q.Reserve(commits,proof.Defaults().size());q.Reserve(initializers,proof.Defaults().size());q.Reserve(captures,proof.Defaults().size());q.Reserve(sites,proof.Defaults().size());q.Reserve(exceptions,proof.Defaults().size());q.Size(exits,g.ops.size());
    for(std::uint32_t i=0;i<g.exceptional_exits.size();++i){q.Step();exits[g.exceptional_exits[i].op.value]=i;}
    for(const auto& d:proof.Defaults()){
      q.Step();const auto& read=g.ops[d.read.value];const auto& origin=g.origins[read.origin.value];const auto& load=std::get<ValueLexicalLoad>(read.operation);
      commits.push_back({d.parameter,d.entry,d.initial_store,d.destination});
      initializers.push_back({d.parameter,*read.result,d.read,{}});captures.push_back({d.parameter,d.read,d.guard,{origin.site.value},load.address,{d.source.depth,d.source.slot}});sites.push_back(static_cast<std::uint32_t>(sites.size()));
      Require(exits[d.guard.value].has_value());exceptions.push_back(g.exceptional_exits[*exits[d.guard.value]]);
    }
    q.Step(sites.size()*std::bit_width(sites.size()+1));std::sort(sites.begin(),sites.end(),[&](auto a,auto b){return captures[a].site.value<captures[b].site.value;});
    DefaultBody body{std::move(residual),std::move(proof),std::move(initializers),{},budget.bytes-q.left.bytes,budget.work-q.left.work};
    body.original_=std::move(original);body.joint_operations_=std::move(operations);body.captures_=std::move(captures);body.commits_=std::move(commits);body.capture_sites_=std::move(sites);body.exceptions_=std::move(exceptions);body.formal_count_=parameters.formals.size();body.length_=length;return body;
  }catch(Error e){return e;}catch(const std::bad_alloc&){return Error::kResourceLimit;}catch(const std::length_error&){return Error::kResourceLimit;}
}
const SignatureCapture* DefaultBody::CaptureAt(analysis::InstructionId site) const {
  const auto it=std::lower_bound(capture_sites_.begin(),capture_sites_.end(),site.value,[&](auto i,auto s){return captures_[i].site.value<s;});
  return it!=capture_sites_.end()&&captures_[*it].site==site?&captures_[*it]:nullptr;
}
const SignatureCapture* DefaultBody::CaptureParameter(std::uint32_t p) const {
  const auto it=std::lower_bound(captures_.begin(),captures_.end(),p,[](const auto& c,auto n){return c.parameter<n;});return it!=captures_.end()&&it->parameter==p?&*it:nullptr;
}
std::variant<CellParameterDefaultCost,CellParameterDefaultFailure> DefaultBody::VerifyCellParameterSource(analysis::SemanticBudget budget) const {
  using E=CellParameterDefaultError;
  try {
    Budget q(budget);
    const auto fail=[](E e){throw CellParameterDefaultFailure{e};};
    const auto require=[&](bool v,E e){if(!v)fail(e);};
    require(original_&&CellParameters(),E::kCorrespondence);
    require(original_->revision==CellParameters()->Revision()&&original_->cfg_revision==CellParameters()->ControlRevision()&&
        graph_.revision.value==original_->revision.value+1&&graph_.cfg_revision.value==original_->cfg_revision.value+1,E::kCorrespondence);
    // Rebuild both storage interpretations; no original CellId is reused as a
    // residual CellId. The commit consumes the initialized FORMAL, never the
    // incoming default cell or the internal result of the guarded read.
    auto env=BuildValueEnvironments(*original_,q.left);const auto* e=std::get_if<ValueEnvironments>(&env);
    if(!e){auto err=std::get<ValueEnvironmentError>(env);fail(err==ValueEnvironmentError::kResourceLimit?E::kResourceLimit:err==ValueEnvironmentError::kWorkLimit?E::kWorkLimit:E::kCorrespondence);}
    q.Admit(e->allocated_bytes,1);q.Step(e->work);
    auto cells=BuildCellPlan(*original_,*e,q.left);const auto* c=std::get_if<CellPlan>(&cells);
    if(!c){auto err=std::get<CellError>(cells);fail(err==CellError::kResourceLimit?E::kResourceLimit:err==CellError::kWorkLimit?E::kWorkLimit:E::kCorrespondence);}
    q.Admit(c->allocated_bytes,1);q.Step(c->work);
    auto parameters=BuildParameterPlan(*original_,formal_count_,q.left);const auto* p=std::get_if<ParameterPlan>(&parameters);
    if(!p){auto err=std::get<ParameterPlanError>(parameters);fail(err==ParameterPlanError::kResourceLimit?E::kResourceLimit:err==ParameterPlanError::kWorkLimit?E::kWorkLimit:E::kCorrespondence);}
    q.Admit(p->allocated_bytes,1);q.Step(p->work);
    auto checked=VerifyCellParameterDefaults(*original_,*e,*c,*p,length_,CellParameters()->Defaults(),q.left);
    if(const auto* f=std::get_if<CellParameterDefaultFailure>(&checked))return *f;q.Cost(std::get<CellParameterDefaultCost>(checked));
    auto residual_env=BuildValueEnvironments(graph_,q.left);const auto* re=std::get_if<ValueEnvironments>(&residual_env);
    if(!re){const auto err=std::get<ValueEnvironmentError>(residual_env);fail(err==ValueEnvironmentError::kResourceLimit?E::kResourceLimit:err==ValueEnvironmentError::kWorkLimit?E::kWorkLimit:E::kBodyState);}
    q.Admit(re->allocated_bytes,1);q.Step(re->work);
    auto residual_cells=BuildCellPlan(graph_,*re,q.left);const auto* rc=std::get_if<CellPlan>(&residual_cells);
    if(!rc){const auto err=std::get<CellError>(residual_cells);fail(err==CellError::kResourceLimit?E::kResourceLimit:err==CellError::kWorkLimit?E::kWorkLimit:E::kBodyState);}
    q.Admit(rc->allocated_bytes,1);q.Step(rc->work);
    std::vector<std::optional<ValueId>> entry;std::vector<std::uint8_t> colors;std::vector<ValueId> path;
    q.Size(entry,graph_.values.size());q.Size(colors,graph_.values.size());q.Reserve(path,graph_.values.size());
    for(std::uint32_t i=0;i<graph_.values.size();++i){q.Step();if(colors[i])continue;ValueId v{i};path.clear();std::optional<ValueId> root;
      for(;;){q.Step();if(colors[v.value]==2){root=entry[v.value];break;}require(colors[v.value]!=1,E::kBodyState);colors[v.value]=1;path.push_back(v);
        const auto& def=graph_.values[v.value];if(const auto* input=std::get_if<EntryDefinition>(&def.origin)){if(std::holds_alternative<ParameterEntry>(*input))root=v;break;}
        const auto* result=std::get_if<OpResult>(&def.origin);if(!result)break;const auto& op=graph_.ops[result->op.value];
        if(op.life!=ValueLife::kLive||!std::holds_alternative<ValueCopy>(op.operation))break;
        require(op.use_count==1,E::kBodyState);const auto* next=std::get_if<ValueId>(&graph_.uses[op.first_use.value].operand);if(!next)break;v=*next;
      }
      for(auto id:path){q.Step();entry[id.value]=root;colors[id.value]=2;}
    }
    const auto defaults=CellParameters()->Defaults();require(commits_.size()==defaults.size()&&captures_.size()==defaults.size()&&exceptions_.size()==defaults.size(),E::kCorrespondence);
    std::vector<std::uint8_t> exits;q.Size(exits,graph_.ops.size());for(const auto& x:graph_.exceptional_exits){q.Step();exits[x.op.value]=1;}
    for(std::size_t i=0;i<defaults.size();++i){q.Step();const auto& d=defaults[i];const auto& commit=commits_[i];const auto& capture=captures_[i];const auto& x=exceptions_[i];
      require(commit.parameter==d.parameter&&commit.initialized_entry==d.entry&&commit.store==d.initial_store&&commit.destination==d.destination,E::kCorrespondence);
      const auto& store=graph_.ops[commit.store.value];const auto* store_op=std::get_if<ValueLexicalStore>(&store.operation);
      q.Step(std::bit_width(rc->cells.size())+1);const auto local=FindCell(*rc,AllocatedCell{d.destination.allocation,d.destination.slot});
      require(local&&store.life==ValueLife::kLive&&store_op&&rc->operations[commit.store.value]==local,E::kBodyState);
      require(store.use_count==1,E::kBodyState);const auto* value=std::get_if<ValueId>(&graph_.uses[store.first_use.value].operand);
      require(value&&entry[value->value]==d.entry,E::kParameterIdentity);
      const auto& read=original_->ops[d.read.value];const auto& load=std::get<ValueLexicalLoad>(read.operation);const auto& origin=original_->origins[read.origin.value];
      require(capture.parameter==d.parameter&&capture.read==d.read&&capture.guard==d.guard&&capture.site.value==origin.site.value&&capture.address.depth.value==load.address.depth.value&&capture.address.slot.value==load.address.slot.value&&capture.storage.depth==d.source.depth&&capture.storage.slot.value==d.source.slot.value,E::kOuterBinding);
      const auto& guard=original_->ops[d.guard.value];require(x.op==d.guard&&x.origin==guard.origin&&x.before_throw==guard.before_throw&&!x.handler&&!exits[d.guard.value],E::kCatchState);
      for(auto op:{d.read,d.guard,d.fallback_store})require(graph_.ops[op.value].life==ValueLife::kRemoved&&std::holds_alternative<ValueNop>(graph_.ops[op.value].operation)&&graph_.ops[op.value].origin==original_->ops[op.value].origin,E::kCorrespondence);
    }
    return CellParameterDefaultCost{budget.bytes-q.left.bytes,budget.work-q.left.work};
  }catch(const CellParameterDefaultFailure& f){return f;}catch(Error e){return CellParameterDefaultFailure{e==Error::kWorkLimit?E::kWorkLimit:e==Error::kResourceLimit?E::kResourceLimit:E::kCorrespondence};}
  catch(const std::bad_alloc&){return CellParameterDefaultFailure{E::kResourceLimit};}catch(const std::length_error&){return CellParameterDefaultFailure{E::kResourceLimit};}
}
}
