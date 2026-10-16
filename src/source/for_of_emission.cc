#include "for_of_emission.h"
#include "../analysis/idf_scratch.h"
#include <stdexcept>
namespace abcd::source {
namespace {
using namespace analysis;
using Error=ForOfIterationError;
struct Quota {
  detail::IdfScratch memory;std::uint64_t initial,left;
  ForOfIterationFailure witness{Error::kCorrespondence};
  explicit Quota(SemanticBudget b):memory(b.bytes),initial(b.work),left(b.work){}
  [[noreturn]] void Fail(Error e){witness.kind=e;throw witness;}
  void Step(std::uint64_t n=1){if(n>left)Fail(Error::kWorkLimit);left-=n;}
  template<class T>void Require(const T& v,Error e=Error::kCorrespondence){if(!v)Fail(e);}
  SemanticBudget Remaining()const{return {memory.Available(),left};}
  template<class T>void Array(std::vector<T>& v,std::size_t n){Require(n<=UINT32_MAX && n<=memory.Available()/sizeof(T),Error::kResourceLimit);Step(n);memory.Retain(n*sizeof(T));v.resize(n);memory.Retain((v.capacity()-n)*sizeof(T));}
};
ForOfEmissionView Build(const ValueProgram& g,const ForOfEvaluationInput& source,
    const ForOfEvaluationPlan& plan,Quota& q) {
  // Re-derive all protocol identity, original edges, close priority and ownership
  // before interpreting the immutable ControlPlan. No builder flag is a proof.
  q.memory.Observe(q.memory.Available());
  const auto checked=VerifyForOfEvaluationPlan(g,source,plan,q.Remaining());
  if(const auto* f=std::get_if<ForOfIterationFailure>(&checked))throw *f;
  q.Step(std::get<std::uint64_t>(checked));
  const auto& control=source.control;ForOfEmissionView out;
  q.Array(out.loops,plan.identity.loops.size());q.Array(out.wrappers,control.skeleton.loops.size());
  q.Array(out.closes,control.units.size());q.Array(out.ports,plan.events.ports.size());q.Array(out.close_blocks,g.blocks.size());
  std::pmr::vector<std::optional<WorkBlockId>> raw_blocks(source.input.flow.nodes.size(),{},&q.memory);
  for(std::uint32_t b=0;b<g.phases.size();++b){q.Step();const auto r=g.phases[b].input;q.Require(r.value<raw_blocks.size() && !raw_blocks[r.value]);raw_blocks[r.value]=WorkBlockId{b};}
  std::pmr::vector<std::optional<ControlId>> nodes(g.blocks.size(),{},&q.memory);
  std::pmr::vector<std::optional<ControlId>> raw(source.input.flow.nodes.size(),{},&q.memory);
  std::pmr::vector<std::optional<LoopId>> headers(control.units.size(),{},&q.memory);
  std::pmr::vector<std::uint8_t> close_raw(source.input.flow.nodes.size(),0,&q.memory);
  for(const auto& r:source.ownership.Summary().regions)for(const auto& member:r.members){q.Step();q.Require(member.node.value<close_raw.size());close_raw[member.node.value]=1;}
  for(std::uint32_t b=0;b<g.phases.size();++b){q.Step();q.Require(g.phases[b].input.value<close_raw.size());out.close_blocks[b]=close_raw[g.phases[b].input.value];}
  for(std::uint32_t n=0;n<control.units.size();++n){q.Step();if(const auto* p=std::get_if<PhaseControlUnit>(&control.units[n])){q.Require(p->block.value<nodes.size() && !nodes[p->block.value]);nodes[p->block.value]=ControlId{n};const auto r=g.phases[p->block.value].input;q.Require(r.value<raw.size() && !raw[r.value]);raw[r.value]=ControlId{n};}}
  for(std::uint32_t l=0;l<control.skeleton.loops.size();++l){q.Step();const auto h=control.skeleton.loops[l].header.value;q.Require(h<headers.size() && !headers[h]);headers[h]=LoopId{l};}
  std::pmr::vector<std::optional<WorkBlockId>> commits(g.ops.size(),{},&q.memory);
  for(const auto& e:plan.events.events){q.Step();q.Require(e.operation.value<commits.size() && !commits[e.operation.value]);commits[e.operation.value]=e.commit;}
  for(std::uint32_t i=0;i<out.loops.size();++i) {
    const auto& loop=plan.identity.loops[i];q.witness={Error::kCorrespondence,loop.protocol,loop.acquisition};
    q.Require(loop.protocol.value==i && nodes[g.ops[loop.advance.value].block.value]);
    std::optional<ControlId> header;
    if(loop.header){q.Step();q.Require(loop.header->value<raw.size());header=raw[loop.header->value];}
    q.Require(header && headers[header->value],Error::kAdvance);
    const auto id=*headers[header->value];q.Require(!out.wrappers[id.value]);out.wrappers[id.value]=i;
    q.Require(commits[loop.decision.value] && nodes[commits[loop.decision.value]->value],Error::kDone);
    const auto decision=*nodes[commits[loop.decision.value]->value];const auto& node=control.skeleton.nodes[decision.value];
    q.Require(node.arms && !node.decision,Error::kDone);
    std::optional<EdgeId> success,exhausted;
    const auto& block=g.blocks[commits[loop.decision.value]->value];
    for(std::uint32_t e=0;e<block.edge_count;++e){q.Step();const EdgeId edge{block.first_edge.value+e};const auto& original=g.edges[edge.value];
      if(original.successor==loop.exhausted.successor){q.Require(!exhausted);exhausted=edge;}
      else if(g.phases[original.target.value].input==loop.success){q.Require(!success);success=edge;}}
    q.Require(success && exhausted,Error::kDone);
    const auto arm=g.edges[success->value].kind==FlowEdgeKind::kTaken?node.arms->taken:node.arms->fallthrough;
    q.Require(arm || node.next,Error::kEntry);
    const auto& acquire=g.ops[loop.acquisition.value];q.Require(acquire.use_count==1);
    out.loops[i]={loop.protocol,id,*header,decision,arm?*arm:*node.next,*success,*exhausted,acquire.first_use,loop.key};
  }
  for(const auto& node:control.skeleton.nodes) {
    q.Step();const auto* transfer=std::get_if<LoopTransfer>(&node.synthetic);
    if(!transfer || transfer->kind!=LoopTransferKind::kBreak || !out.wrappers[transfer->loop.value])continue;
    const auto i=*out.wrappers[transfer->loop.value];const auto* edge=std::get_if<EdgeId>(&transfer->source);
    if(!edge || edge->value>=control.arcs.size() || control.arcs[edge->value].edge!=out.loops[i].exhausted)out.loops[i].exhaustion_only=false;
  }
  // A single indexed port census, not a scan of the full table per entry.
  std::uint32_t count=0;
  for(std::uint32_t n=0;n<control.units.size();++n) {
    q.Step();const auto* unit=std::get_if<ProtocolCloseUnit>(&control.units[n]);if(!unit)continue;
    q.Require(unit->protocol.value<out.loops.size());
    const auto first=count;
    for(std::uint32_t p=0;p<unit->port_count;++p){q.Step();q.Require(count<out.ports.size());const auto& port=plan.events.ports[count];q.Require(port.unit==ControlNodeId{n} && port.protocol==unit->protocol && port.entry==unit->entry && port.pending==unit->pending);out.ports[count++]=port;}
    q.Require(count>first);
    q.Require(unit->entry.source.value<raw_blocks.size() && raw_blocks[unit->entry.source.value]);
    const auto& block=g.blocks[raw_blocks[unit->entry.source.value]->value];std::optional<EdgeId> edge;
    for(std::uint32_t e=0;e<block.edge_count;++e){q.Step();const EdgeId id{block.first_edge.value+e};if(g.edges[id.value].successor==unit->entry.successor){q.Require(!edge);edge=id;}}
    q.Require(edge);std::optional<ForOfExitPayload> payload;
    if(const auto* v=std::get_if<SsaValueId>(&unit->pending.payload)){q.Require(v->value<g.input.values.size());payload=g.input.values[v->value];}
    else if(unit->pending.kind==PendingCompletionKind::kReturn && std::holds_alternative<std::monostate>(unit->pending.payload))payload=ForOfUndefined{};
    out.closes[n]=ForOfEmissionClose{unit->protocol,unit->entry,unit->pending,first,count-first,*edge,payload};
  }
  q.Require(count==out.ports.size());
  out.allocated_bytes=out.loops.capacity()*sizeof(ForOfEmissionLoop)+out.wrappers.capacity()*sizeof(out.wrappers[0])+out.closes.capacity()*sizeof(out.closes[0])+out.ports.capacity()*sizeof(ForOfClosePort)+out.close_blocks.capacity();
  out.peak_bytes=q.memory.Peak();out.work=q.initial-q.left;return out;
}
}
std::variant<ForOfEmissionView,ForOfIterationFailure> BuildForOfEmissionView(
    const analysis::ValueProgram& g,const ForOfEvaluationInput& i,const ForOfEvaluationPlan& p,analysis::SemanticBudget b) {
  Quota q(b);try{return Build(g,i,p,q);}catch(const ForOfIterationFailure& f){return f;}
  catch(const std::bad_alloc&){return ForOfIterationFailure{Error::kResourceLimit};}
  catch(const std::length_error&){return ForOfIterationFailure{Error::kResourceLimit};}
}
std::variant<std::uint64_t,ForOfIterationFailure> VerifyForOfEmissionView(
    const analysis::ValueProgram& g,const ForOfEvaluationInput& i,const ForOfEvaluationPlan& p,
    const ForOfEmissionView& view,analysis::SemanticBudget b) {
  auto built=BuildForOfEmissionView(g,i,p,b);if(const auto* f=std::get_if<ForOfIterationFailure>(&built))return *f;
  const auto& actual=std::get<ForOfEmissionView>(built);const auto cost=view.loops.size()+view.wrappers.size()+view.closes.size()+view.ports.size()+view.close_blocks.size();
  if(cost>b.work-actual.work)return ForOfIterationFailure{Error::kWorkLimit};
  if(actual.loops!=view.loops || actual.wrappers!=view.wrappers || actual.closes!=view.closes || actual.ports!=view.ports || actual.close_blocks!=view.close_blocks)return ForOfIterationFailure{Error::kCorrespondence};
  return actual.work+cost;
}
}
