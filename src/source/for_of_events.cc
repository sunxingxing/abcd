#include "for_of_events.h"
#include "../analysis/idf_scratch.h"
#include <stdexcept>
namespace abcd::source {
namespace {
using namespace analysis;
using Error=ForOfIterationError;
struct Quota {
  detail::IdfScratch memory;
  std::uint64_t initial,left;
  ForOfIterationFailure witness{Error::kCorrespondence};
  explicit Quota(SemanticBudget b):memory(b.bytes),initial(b.work),left(b.work){}
  void Step(std::uint64_t n=1){if(n>left)Fail(Error::kWorkLimit);left-=n;}
  [[noreturn]] void Fail(Error e){witness.kind=e;throw witness;}
  template<class T>void Require(const T& v,Error e=Error::kCorrespondence){if(!v)Fail(e);}
  SemanticBudget Remaining()const{return {memory.Available(),left};}
  template<class T>void Reserve(std::vector<T>& v,std::size_t n){Require(n<=UINT32_MAX && n<=memory.Available()/sizeof(T),Error::kResourceLimit);Step(n);memory.Retain(n*sizeof(T));v.reserve(n);memory.Retain((v.capacity()-n)*sizeof(T));}
};
std::variant<ForOfEvents,ForOfIterationFailure> Build(
    const abc::MethodBody& body,const ValueProgram& g,const PhasedValueInput& input,
    const ExceptionPlan& regions,const ExceptionControl& control,const VerifiedProtocolRegionOwnership& owner,
    SourceContract contract,const ForOfIdentityIndex& identity,const ForOfIterationProof& iteration,SemanticBudget budget) {
  try {
    Quota q(budget);
    // The input certificates are checked against original flow and B's sealed
    // ownership before any event can be claimed. The exclusive grant is only
    // observed as a peak; delegated verification retains no allocation here.
    q.memory.Observe(q.memory.Available());
    auto verified=VerifyForOfIterations(body,g,input,regions,control,owner,contract,identity,iteration,q.Remaining());
    if(const auto* f=std::get_if<ForOfIterationFailure>(&verified))return *f;
    q.Step(std::get<std::uint64_t>(verified));
    std::pmr::vector<std::optional<WorkBlockId>> commits(g.ops.size(),{},&q.memory);
    std::pmr::vector<std::optional<std::uint32_t>> exits(g.ops.size(),{},&q.memory);
    for(std::uint32_t b=0;b<g.phases.size();++b){q.Step();const auto& p=g.phases[b];if(p.phase==ExceptionPhase::kCommit && p.operation){q.Require(p.operation->value<commits.size() && !commits[p.operation->value]);commits[p.operation->value]=WorkBlockId{b};}}
    for(std::uint32_t e=0;e<g.exceptional_exits.size();++e){q.Step();const auto o=g.exceptional_exits[e].op;q.Require(o.value<exits.size() && !exits[o.value],Error::kCorrespondence);exits[o.value]=e;}
    ForOfEvents out{g.revision,g.cfg_revision};
    constexpr std::size_t kEventsPerLoop=7;
    q.Require(identity.loops.size()<=UINT32_MAX/kEventsPerLoop,Error::kResourceLimit);
    q.Reserve(out.events,identity.loops.size()*kEventsPerLoop);
    q.Reserve(out.commits,iteration.definitions.size());q.Reserve(out.ports,control.protocol_ports.size());
    const auto event=[&](IteratorProtocolId protocol,ForOfEventKind kind,OpId id){
      q.Step();q.witness={Error::kCorrespondence,protocol,id};q.Require(id.value<g.ops.size());const auto& op=g.ops[id.value];q.Require(op.life==ValueLife::kLive && commits[id.value]);
      const auto commit=*commits[id.value];const auto& before=g.phases[op.block.value];const auto& after=g.phases[commit.value];
      q.Require(before.phase==ExceptionPhase::kBefore && before.operation==id && after.operation==id && after.phase==ExceptionPhase::kCommit);
      if(op.result){const auto* def=std::get_if<OpResult>(&g.values[op.result->value].origin);q.Require(def && def->op==id && def->commit==commit);}
      q.Require(exits[id.value].has_value()==(op.exceptional==ExceptionalCompletion::kMayThrow));
      bool normal=false,raised=false;const auto& block=g.blocks[op.block.value];
      for(std::uint32_t n=0;n<block.edge_count;++n){q.Step();const auto& edge=g.edges[block.first_edge.value+n];
        if(edge.kind==FlowEdgeKind::kException){q.Require(!raised && exits[id.value]);const auto& exit=g.exceptional_exits[*exits[id.value]];q.Require(exit.handler && edge.target==exit.handler->entry);raised=true;}
        else {q.Require(!normal && edge.kind==FlowEdgeKind::kFallthrough && edge.target==commit);normal=true;}}
      q.Require(normal);
      if(exits[id.value]){const auto& exit=g.exceptional_exits[*exits[id.value]];q.Require(exit.op==id && exit.origin==op.origin && exit.before_throw==op.before_throw && raised==exit.handler.has_value());}
      else q.Require(!raised);
      out.events.push_back({protocol,kind,id,op.block,commit,before.input,after.input,op.origin,op.result,exits[id.value]});
    };
    for(const auto& l:identity.loops){
      event(l.protocol,ForOfEventKind::kAcquire,l.acquisition);event(l.protocol,ForOfEventKind::kNextRead,l.next);event(l.protocol,ForOfEventKind::kNextCall,l.advance);event(l.protocol,ForOfEventKind::kObjectCheck,l.check);event(l.protocol,ForOfEventKind::kDoneRead,l.done);event(l.protocol,ForOfEventKind::kDoneDecision,l.decision);event(l.protocol,ForOfEventKind::kValueRead,l.value);
    }
    for(const auto& d:iteration.definitions){q.Step();q.witness={Error::kBinding,d.protocol,d.operation};q.Require(d.protocol.value<identity.loops.size() && commits[d.operation.value]);const auto& loop=identity.loops[d.protocol.value];const auto decision=commits[loop.decision.value];q.Require(decision);std::optional<CompletionEdge> success;
      const auto& block=g.blocks[decision->value];for(std::uint32_t n=0;n<block.edge_count;++n){q.Step();const auto& edge=g.edges[block.first_edge.value+n];if(g.phases[edge.target.value].input==loop.success){q.Require(!success);success=CompletionEdge{g.phases[decision->value].input,edge.successor};}}
      q.Require(success);out.commits.push_back({d,*commits[d.operation.value],g.phases[commits[d.operation.value]->value].input,*success,loop.exhausted});
    }
    for(std::uint32_t n=0;n<control.units.size();++n){q.Step();if(const auto* unit=std::get_if<ProtocolCloseUnit>(&control.units[n])){
      q.witness={Error::kCorrespondence,unit->protocol};q.Require(unit->first_port<=control.protocol_ports.size() && unit->port_count<=control.protocol_ports.size()-unit->first_port);
      for(std::uint32_t p=0;p<unit->port_count;++p){q.Step();q.Require(out.ports.size()<out.ports.capacity(),Error::kCorrespondence);out.ports.push_back({unit->protocol,{n},unit->entry,unit->pending,control.protocol_ports[unit->first_port+p]});}
    }}
    q.Require(out.ports.size()==control.protocol_ports.size());
    out.allocated_bytes=out.events.capacity()*sizeof(ForOfEvent)+out.commits.capacity()*sizeof(ForOfValueCommit)+out.ports.capacity()*sizeof(ForOfClosePort);out.peak_bytes=q.memory.Peak();out.work=q.initial-q.left;return out;
  }catch(const ForOfIterationFailure& f){return f;}catch(const std::bad_alloc&){return ForOfIterationFailure{Error::kResourceLimit};}catch(const std::length_error&){return ForOfIterationFailure{Error::kResourceLimit};}
}
}
std::variant<ForOfEvents,ForOfIterationFailure> BuildForOfEvents(
    const abc::MethodBody& b,const ValueProgram& g,const PhasedValueInput& i,const ExceptionPlan& r,
    const ExceptionControl& c,const VerifiedProtocolRegionOwnership& o,SourceContract s,
    const ForOfIdentityIndex& x,const ForOfIterationProof& p,SemanticBudget q){return Build(b,g,i,r,c,o,s,x,p,q);}
std::variant<std::uint64_t,ForOfIterationFailure> VerifyForOfEvents(
    const abc::MethodBody& b,const ValueProgram& g,const PhasedValueInput& i,const ExceptionPlan& r,
    const ExceptionControl& c,const VerifiedProtocolRegionOwnership& o,SourceContract s,
    const ForOfIdentityIndex& x,const ForOfIterationProof& p,const ForOfEvents& evidence,SemanticBudget q){
  auto built=Build(b,g,i,r,c,o,s,x,p,q);if(const auto* f=std::get_if<ForOfIterationFailure>(&built))return *f;
  const auto& expected=std::get<ForOfEvents>(built);const auto cost=expected.events.size()+expected.commits.size()+expected.ports.size()+1;
  if(cost>q.work-expected.work)return ForOfIterationFailure{Error::kWorkLimit};
  if(evidence.revision!=g.revision || evidence.cfg_revision!=g.cfg_revision || evidence.events!=expected.events || evidence.commits!=expected.commits || evidence.ports!=expected.ports)return ForOfIterationFailure{Error::kCorrespondence};
  return expected.work+cost;
}
}
