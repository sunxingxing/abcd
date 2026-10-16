// Read-only standalone certificate diagnostic. No production admission hook.
#include "../../../src/source/for_of_iteration.h"
#include "../../../src/source/for_of_events.h"
#include "../../../src/analysis/value_edits.h"
#include "../../../src/source/exception_control.h"
#include "../../../src/source/exception_topology.h"
#include "abcd/source/async_completion.h"
#include "abcd/analysis/exception_locals.h"
#include "abcd/analysis/exception_captures.h"
#include "abcd/analysis/exception_modules.h"
#include "abcd/abc/call_type.h"
#include "abcd/corpus/abc_file.h"
#include "analysis/ssa_fixture.h"
#include <iostream>
#include <algorithm>
#include <map>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
struct Refused {};
template<class T,class...U>T Take(std::variant<U...> value,const char* stage) {
  if(auto* p=std::get_if<T>(&value))return std::move(*p);
  std::cout<<"stage="<<stage<<" alternative="<<value.index();
  std::visit([](const auto& f){
    if constexpr(std::is_enum_v<std::decay_t<decltype(f)>>)std::cout<<" enum="<<unsigned(f);
    if constexpr(requires{f.kind;})std::cout<<" kind="<<unsigned(f.kind);
    if constexpr(requires{f.pc;}) {if constexpr(requires{f.pc.value;})std::cout<<" pc="<<f.pc.value;else if(f.pc)std::cout<<" pc="<<f.pc->value;}
  },value);std::cout<<'\n';throw Refused{};
}
void Case(const char* path,unsigned padding,unsigned entity=451,unsigned shift=0,bool split=false,bool expected_gap=false) {
  try {
    auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path),"file");
    auto hash=Take<corpus::FileFingerprint>(file->Fingerprint(),"fingerprint");
    auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size),"header");
    auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header),"metadata");

    const auto method=std::find_if(index.methods.begin(),index.methods.end(),[&](const auto& m){return m.offset.value==entity;});
    test::Check(method!=index.methods.end() && method->code);
    const auto call=Take<abc::CallType>(abc::ReadCallType(*file,header,index,method->annotations),"calltype");
    const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(method->code->registers+shift,method->code->arguments,abc::DecodeCallFlags(call.value)),"frame");
    auto body=Take<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*method->code),"body");
    if(shift)for(auto& i:body.instructions)for(auto& o:std::span(i.decoded.operands).first(i.decoded.operand_count))if(o.kind==abc::OperandKind::kRegister){test::Check(o.raw_bits+shift<(std::uint64_t{1}<<o.width_bits));o.raw_bits+=shift;}
    if(split){
      std::map<std::uint32_t,std::uint32_t> positions;std::uint32_t end=0;
      for(const auto& i:body.instructions){positions[i.offset.value]=end;end+=i.decoded.size+1;}
      const auto old_end=body.instructions.back().offset.value+body.instructions.back().decoded.size;positions[old_end]=end;
      auto nop=test::Body({0xd5}).instructions.front();std::vector<abc::BodyInstruction> changed;
      for(auto i:body.instructions){const auto at=positions.at(i.offset.value);nop.offset={at};changed.push_back(nop);i.offset={at+1};if(i.branch_target){i.branch_target=abc::CodeOffset{positions.at(i.branch_target->value)};if(i.decoded.branch_delta)i.decoded.branch_delta=std::int64_t{i.branch_target->value}-i.offset.value;}changed.push_back(i);}
      for(auto& r:body.tries){const auto first=positions.at(r.offset.value),last=positions.at(r.offset.value+r.length);r.offset={first};r.length=last-first;}
      for(auto& h:body.handlers){if(h.length)h.length=positions.at(h.offset.value+h.length)-positions.at(h.offset.value);h.offset={positions.at(h.offset.value)};}
      body.instructions=std::move(changed);
    }
    std::vector<abc::CodeOffset> origins;std::vector<AwaitSite> awaits;
    if(padding) {
      const auto nop=test::Body({0xd5}).instructions.front();
      for(auto& i:body.instructions){i.offset.value+=padding;if(i.branch_target)i.branch_target->value+=padding;}
      for(auto& t:body.tries)t.offset.value+=padding;
      for(auto& h:body.handlers)h.offset.value+=padding;
      body.instructions.insert(body.instructions.begin(),padding,nop);
      for(unsigned i=0;i<padding;++i)body.instructions[i].offset={i};
    }
    auto flow=Take<ExceptionFlow>(BuildExceptionFlow(body,header.profile,frame.registers,{},awaits,&frame),"flow");
    for(std::size_t i=0;i<origins.size();++i)flow.instructions[i].origin=origins[i];
    IdfQuota quota{kDefaultSemanticWork};
    auto ssa=Take<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,quota),"ssa");
    std::vector<EntryValueFact> entry;
    for(std::uint32_t v=0;v<ssa.Values().size();++v)if(std::holds_alternative<MethodEntry>(ssa.Values()[v].origin))
      if(const auto* reg=std::get_if<abc::RegisterId>(&ssa.Values()[v].slot);reg && reg->value>=method->code->registers+shift)
        entry.push_back({{v},ValueKinds::Language()});
    auto env=Take<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow),"environments");
    // These are the SAME conditional premises as PhasedBody, not filled-in
    // Language facts: the ordinary source owner must still close captures and
    // transitive closure writes. This probe never publishes source/ownership.
    auto locals=Take<ExceptionLocals>(ProveExceptionLocals(flow,env,{}, {},ClosureWrites::kLanguage),"locals");
    auto captures=Take<ExceptionCaptures>(PlanExceptionCaptures(flow,env),"captures");
    auto modules=Take<ExceptionModules>(ProveExceptionModules(flow,ssa),"modules");
    std::vector<ExceptionLoadFact> loads=locals.loads;
    loads.insert(loads.end(),captures.loads.begin(),captures.loads.end());
    loads.insert(loads.end(),modules.loads.begin(),modules.loads.end());
    auto values=Take<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entry,quota,{},PendingExceptionValues::kLanguage,loads, {},CallResultValues::kLanguage,FrameReceiver::kUnknown,Arithmetic(SourceContract::kLanguageSourceV4)),"values");
    auto checked=VerifyExceptionInputs(flow,ssa,env,values,kDefaultSemanticWork,nullptr,ExceptionInputDomain::kHoleTransport);
    if(const auto* f=std::get_if<ExceptionInputFailure>(&checked)) {
      std::cout<<"input-failure kind="<<unsigned(f->kind)<<" working-pc="<<f->pc.value;
      if(f->node)std::cout<<" node="<<f->node->value;
      if(f->value) {
        std::cout<<" value="<<f->value->value;
        const auto& origin=ssa.Values()[f->value->value].origin;
        if(const auto* site=std::get_if<InstructionValue>(&origin))std::cout<<" defined-pc="<<flow.instructions[flow.nodes[site->instruction.value].instruction.value].pc.value;
        if(std::holds_alternative<PhiValue>(origin))std::cout<<" phi=1";
        for(auto kind:{VmValueKind::kLanguage,VmValueKind::kHole,VmValueKind::kEnvironment,VmValueKind::kOtherInternal,VmValueKind::kPropertyEnumerator})
          if(values.Facts()[f->value->value].Contains(kind))std::cout<<" category="<<unsigned(kind);
      }
      std::cout<<'\n';
    }
    auto proof=Take<ExceptionInputProof>(std::move(checked),"inputs");
    PhasedValueInput input{flow,ssa,env,values,proof,frame};
    auto graph=Take<ValueProgram>(ImportPhasedValues(input),"import");
    auto plan=Take<ExceptionPlan>(BuildExceptionPlan(body,graph,input),"plan");
    auto keys=Take<IteratorKeys>(ResolveIteratorKeys({*file,header,index},method->offset,graph),"keys");
    auto scan=Take<IteratorProtocolScan>(ScanIteratorProtocols(graph,input,plan,keys),"scan");
    auto summary=Take<CloseCompletionSummary>(AnalyzeCloseCompletion(body,header.profile,graph,input,plan,keys,scan,{},SourceContract::kLanguageSourceV4),"summary");
    const auto seal=[&](CloseCompletionSummary value,SourceContract contract=SourceContract::kLanguageSourceV4) {
      return VerifyProtocolRegionOwnership(body,header.profile,graph,input,plan,keys,scan,std::move(value),{},contract);
    };
    test::Check(std::holds_alternative<CloseCompletionFailure>(seal(summary,SourceContract::kLanguageSourceV3)));
    auto bad=summary;bad.entries[0].pending.payload=CompletionEntryPayload{999};
    test::Check(std::holds_alternative<CloseCompletionFailure>(seal(std::move(bad))));
    bad=summary;bad.regions[0].boundary->ports.pop_back();
    test::Check(std::holds_alternative<CloseCompletionFailure>(seal(std::move(bad))));
    bad=summary;bad.regions[0].boundary->regions.push_back({999});
    test::Check(std::holds_alternative<CloseCompletionFailure>(seal(std::move(bad))));
    auto sealed=seal(std::move(summary));
    if(expected_gap){test::Check(std::holds_alternative<CloseCompletionFailure>(sealed));const auto& f=std::get<CloseCompletionFailure>(sealed);test::Check(f.kind==CloseCompletionError::kOwnership);std::cout<<"live-out fixture retains CloseCompletionError::kOwnership\n";return;}
    auto owner=Take<VerifiedProtocolRegionOwnership>(std::move(sealed),"seal");
    auto built_control=BuildExceptionControl(body,graph,input,plan,{},&owner);
    auto control=Take<ExceptionControl>(std::move(built_control),"control");
    const auto verify=[&](const ForOfIdentityIndex& value) {
      return VerifyForOfIdentityIndex(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,value);
    };
    auto protocol_result=IndexForOfIdentities(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4);
    if(const auto* f=std::get_if<ForOfIdentityFailure>(&protocol_result)) {
      std::cerr<<"forof kind="<<unsigned(f->kind);
      if(f->operation)std::cerr<<" op="<<f->operation->value<<" pc="<<graph.origins[graph.ops[f->operation->value].origin.value].pc.value;
      if(f->use)std::cerr<<" use="<<f->use->value;
      std::cerr<<'\n';
    }
    auto protocol=Take<ForOfIdentityIndex>(std::move(protocol_result),"forof");
    Take<std::uint64_t>(verify(protocol),"verify-forof");
    auto advancing=ProveForOfIterations(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol);
    if(const auto* f=std::get_if<ForOfIterationFailure>(&advancing)){std::cerr<<"iteration kind="<<unsigned(f->kind);if(f->node)std::cerr<<" node="<<f->node->value;if(f->operation)std::cerr<<" op="<<f->operation->value;if(f->edge)std::cerr<<" edge="<<f->edge->value;std::cerr<<'\n';}
    auto iteration=Take<ForOfIterationProof>(std::move(advancing),"iteration");
    const auto verify_iteration=[&](const ForOfIterationProof& value){return VerifyForOfIterations(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol,value);};
    Take<std::uint64_t>(verify_iteration(iteration),"verify-iteration");
    test::Check(iteration.references.size()==protocol.loops.size()*3);
    auto bad_reference=iteration;bad_reference.references[0].root=protocol.loops[0].advance;
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(bad_reference)));
    bad_reference=iteration;std::swap(bad_reference.references[1].use,bad_reference.references[2].use);
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(bad_reference)));
    auto events=Take<ForOfEvents>(BuildForOfEvents(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol,iteration),"events");
    const auto verify_events=[&](const ForOfEvents& e){return VerifyForOfEvents(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol,iteration,e);};
    Take<std::uint64_t>(verify_events(events),"verify-events");
    auto wrong_event=events;wrong_event.events[0].commit=wrong_event.events[0].before;
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_events(wrong_event)));
    wrong_event=events;wrong_event.events[2].exception_exit.reset();
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_events(wrong_event)));
    wrong_event=events;wrong_event.commits[0].success=wrong_event.commits[0].exhausted;
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_events(wrong_event)));
    wrong_event=events;wrong_event.ports[0].pending.target=ExceptionNodeId{0};
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_events(wrong_event)));
    const auto event_build=[&](SemanticBudget b){return BuildForOfEvents(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol,iteration,b);};
    Take<ForOfEvents>(event_build({SemanticBudget{}.bytes,events.work}),"events-exact-work");
    test::Check(std::get<ForOfIterationFailure>(event_build({SemanticBudget{}.bytes,events.work-1})).kind==ForOfIterationError::kWorkLimit);
    std::size_t elow=0,ehigh=SemanticBudget{}.bytes;
    while(elow<ehigh){const auto mid=elow+(ehigh-elow)/2;auto e=event_build({mid,SemanticBudget{}.work});if(std::holds_alternative<ForOfEvents>(e))ehigh=mid;else{test::Check(std::get<ForOfIterationFailure>(e).kind==ForOfIterationError::kResourceLimit);elow=mid+1;}}
    Take<ForOfEvents>(event_build({elow,SemanticBudget{}.work}),"events-exact-bytes");
    test::Check(elow>0 && std::get<ForOfIterationFailure>(event_build({elow-1,SemanticBudget{}.work})).kind==ForOfIterationError::kResourceLimit);
    std::cout<<"references="<<iteration.references.size()<<" events="<<events.events.size()<<" commits="<<events.commits.size()<<" ports="<<events.ports.size()<<'\n';
    for(const auto contract:{kSourceContract,SourceContract::kLanguageSourceV1})test::Check(std::get<ForOfIterationFailure>(ProveForOfIterations(body,graph,input,plan,control,owner,contract,protocol)).kind==ForOfIterationError::kAcquisitionSemantics);
    auto altered=iteration;altered.iterations[0].advance=altered.iterations[0].decision;
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    altered=iteration;std::swap(altered.iterations[0].success,altered.iterations[0].exhausted);
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    test::Check(!iteration.definitions.empty());altered=iteration;altered.definitions[0].original.value^=1;
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    altered=iteration;altered.definitions.pop_back();test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    const auto advancing_build=[&](SemanticBudget b){return ProveForOfIterations(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol,b);};
    Take<ForOfIterationProof>(advancing_build({SemanticBudget{}.bytes,iteration.work}),"iteration-exact-work");
    test::Check(std::get<ForOfIterationFailure>(advancing_build({SemanticBudget{}.bytes,iteration.work-1})).kind==ForOfIterationError::kWorkLimit);
    altered=iteration;altered.definitions.push_back(altered.definitions.front());test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    altered=iteration;altered.definitions[0].operation=protocol.loops[0].decision;test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    altered=iteration;altered.iterations[0].value=altered.iterations[0].header;test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    auto forged_control=control;test::Check(!forged_control.protocol_ports.empty());forged_control.protocol_ports[0].pending.target=ExceptionNodeId{0};
    test::Check(std::holds_alternative<ForOfIterationFailure>(ProveForOfIterations(body,graph,input,plan,forged_control,owner,SourceContract::kLanguageSourceV4,protocol)));
    auto deleted_use=graph;test::Check(!deleted_use.uses.empty());deleted_use.uses.front().life=ValueLife::kRemoved;
    test::Check(std::holds_alternative<ForOfIterationFailure>(ProveForOfIterations(body,deleted_use,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol)));
    auto swapped_reference=graph;
    const auto& next_call=graph.ops[protocol.loops[0].advance.value];
    std::optional<UseId> callee_use,receiver_use;
    for(std::uint32_t u=next_call.first_use.value;u<next_call.first_use.value+next_call.use_count;++u){if(graph.uses[u].role==OperandRole::kCallee)callee_use=UseId{u};if(graph.uses[u].role==OperandRole::kReceiver)receiver_use=UseId{u};}
    test::Check(callee_use && receiver_use);
    const auto callee_value=std::get<ValueId>(graph.uses[callee_use->value].operand),receiver_value=std::get<ValueId>(graph.uses[receiver_use->value].operand);
    test::Check(!ReplaceUse(swapped_reference,*callee_use,receiver_value));
    test::Check(!ReplaceUse(swapped_reference,*receiver_use,callee_value));
    test::Check(!VerifyValueUses(swapped_reference,swapped_reference.uses.size()));
    test::Check(std::holds_alternative<ForOfIterationFailure>(ProveForOfIterations(body,swapped_reference,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol)));
    auto escape=graph;std::optional<UseId> external_use;
    for(std::uint32_t u=0;u<graph.uses.size();++u)if(graph.uses[u].life==ValueLife::kLive && !protocol.uses[u] && std::holds_alternative<ValueId>(graph.uses[u].operand) && std::holds_alternative<OpId>(graph.uses[u].owner)){external_use=UseId{u};break;}
    test::Check(external_use && graph.ops[protocol.loops[0].acquisition.value].result);
    test::Check(!ReplaceUse(escape,*external_use,*graph.ops[protocol.loops[0].acquisition.value].result));
    test::Check(!VerifyValueUses(escape,escape.uses.size()));
    test::Check(std::holds_alternative<ForOfIterationFailure>(ProveForOfIterations(body,escape,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol)));
    auto missing_exit=graph;const auto call_exit=std::find_if(missing_exit.exceptional_exits.begin(),missing_exit.exceptional_exits.end(),[&](const auto& e){return e.op==protocol.loops[0].advance;});test::Check(call_exit!=missing_exit.exceptional_exits.end());missing_exit.exceptional_exits.erase(call_exit);
    test::Check(std::holds_alternative<ForOfIterationFailure>(BuildForOfEvents(body,missing_exit,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol,iteration)));
    auto wrong_identity=protocol;wrong_identity.loops[0].next=wrong_identity.loops[0].advance;
    test::Check(std::holds_alternative<ForOfIterationFailure>(ProveForOfIterations(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,wrong_identity)));
    if(!iteration.phis.empty()){altered=iteration;altered.phis[0].edge.value^=1;test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));}
    std::size_t ilow=0,ihigh=SemanticBudget{}.bytes;
    while(ilow<ihigh){const auto middle=ilow+(ihigh-ilow)/2;auto trial=advancing_build({middle,SemanticBudget{}.work});if(std::holds_alternative<ForOfIterationProof>(trial))ihigh=middle;else {test::Check(std::get<ForOfIterationFailure>(trial).kind==ForOfIterationError::kResourceLimit);ilow=middle+1;}}
    Take<ForOfIterationProof>(advancing_build({ilow,SemanticBudget{}.work}),"iteration-exact-bytes");
    test::Check(ilow>0 && std::get<ForOfIterationFailure>(advancing_build({ilow-1,SemanticBudget{}.work})).kind==ForOfIterationError::kResourceLimit);
    std::cout<<"advancing="<<iteration.iterations.size()<<" definitions="<<iteration.definitions.size()<<" phis="<<iteration.phis.size()<<" work="<<iteration.work<<'\n';
    for(auto contract:{kSourceContract,SourceContract::kLanguageSourceV1}) {
      auto refused=IndexForOfIdentities(body,graph,input,plan,control,owner,contract);
      test::Check(std::get<ForOfIdentityFailure>(refused).kind==ForOfIdentityError::kAcquisitionSemantics);
    }
    const auto reject=[&](ForOfIdentityIndex value){test::Check(std::holds_alternative<ForOfIdentityFailure>(verify(value)));};
    auto wrong=protocol;wrong.loops[0].iterator=wrong.loops[0].result;reject(std::move(wrong));
    wrong=protocol;wrong.loops[0].next=wrong.loops[0].advance;reject(std::move(wrong));
    wrong=protocol;wrong.loops[0].exhausted.successor^=1;reject(std::move(wrong));
    wrong=protocol;wrong.operations[wrong.loops[0].check.value].reset();reject(std::move(wrong));
    wrong=protocol;wrong.uses.clear();reject(std::move(wrong));
    wrong=protocol;wrong.values[wrong.loops[0].iterator.value].reset();reject(std::move(wrong));
    const auto build=[&](SemanticBudget b){return IndexForOfIdentities(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,b);};
    Take<ForOfIdentityIndex>(build({SemanticBudget{}.bytes,protocol.work}),"exact-work");
    auto low_work=build({SemanticBudget{}.bytes,protocol.work-1});
    test::Check(std::get<ForOfIdentityFailure>(low_work).kind==ForOfIdentityError::kWorkLimit);
    std::size_t lo=0,hi=SemanticBudget{}.bytes;
    while(lo<hi){const auto mid=lo+(hi-lo)/2;auto trial=build({mid,SemanticBudget{}.work});
      if(std::holds_alternative<ForOfIdentityIndex>(trial))hi=mid;
      else {test::Check(std::get<ForOfIdentityFailure>(trial).kind==ForOfIdentityError::kResourceLimit);lo=mid+1;}}
    Take<ForOfIdentityIndex>(build({lo,SemanticBudget{}.work}),"exact-bytes");
    test::Check(lo>0 && std::get<ForOfIdentityFailure>(build({lo-1,SemanticBudget{}.work})).kind==ForOfIdentityError::kResourceLimit);
    std::cout<<"padding="<<padding<<" loops="<<protocol.loops.size()<<" work="<<protocol.work<<'\n';
  }catch(const Refused&){test::Check(false);}
}
int main(int argc,char**argv) {
  test::Check(argc>=2 && argc<=4);
  if(argc>=3)Case(argv[1],0,std::stoul(argv[2]),0,false,argc==4 && std::string_view(argv[3])=="--expect-ownership-gap");
  else {for(unsigned padding:{0,200,201,400,800})Case(argv[1],padding);Case(argv[1],0,451,1);Case(argv[1],0,451,0,true);}
}
