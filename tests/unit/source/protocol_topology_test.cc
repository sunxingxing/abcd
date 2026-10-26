// Read-only standalone certificate diagnostic. No production admission hook.
#include "../../../src/source/close_completion.h"
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
void Case(const char* path,unsigned padding,unsigned entity) {
  try {
    auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path),"file");
    auto hash=Take<corpus::FileFingerprint>(file->Fingerprint(),"fingerprint");
    auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size),"header");
    auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header),"metadata");

    const auto method=std::find_if(index.methods.begin(),index.methods.end(),[&](const auto& m){return m.offset.value==entity;});
    test::Check(method!=index.methods.end() && method->code);
    const auto call=Take<abc::CallType>(abc::ReadCallType(*file,header,index,method->annotations),"calltype");
    const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(method->code->registers,method->code->arguments,abc::DecodeCallFlags(call.value)),"frame");
    auto body=Take<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*method->code),"body");
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
      if(const auto* reg=std::get_if<abc::RegisterId>(&ssa.Values()[v].slot);reg && reg->value>=method->code->registers)
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
    auto values=Take<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entry,quota,{},PendingExceptionValues::kLanguage,loads, {},CallResultValues::kLanguage,FrameReceiver::kUnknown,kSourceArithmetic),"values");
    auto checked=VerifyExceptionInputs(flow,ssa,env,values,kDefaultSemanticWork,nullptr,ExceptionInputDomain::kHoleTransport);
    if(const auto* f=std::get_if<ExceptionInputFailure>(&checked);f && f->kind==ExceptionInputError::kInternalValue) {
      values=Take<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entry,quota,{},PendingExceptionValues::kLanguage,loads, {},CallResultValues::kLanguage,FrameReceiver::kUnknown,kSourceArithmetic,ExceptionThrowRefinement::kGuardedNonHole),"refined-values");
      checked=VerifyExceptionInputs(flow,ssa,env,values,kDefaultSemanticWork,nullptr,ExceptionInputDomain::kHoleTransport);
    }
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
    const auto enclosing=std::find_if(summary.regions.begin(),summary.regions.end(),[](const auto& r){return r.boundary && !r.boundary->enclosures.empty();});
    if(enclosing!=summary.regions.end()) {
      bad=summary;bad.regions[enclosing-summary.regions.begin()].boundary->enclosures.clear();
      test::Check(std::holds_alternative<CloseCompletionFailure>(seal(std::move(bad))));
      bad=summary;bad.regions[enclosing-summary.regions.begin()].boundary->enclosures[0]={999};
      test::Check(std::holds_alternative<CloseCompletionFailure>(seal(std::move(bad))));
    }
    auto owner=Take<VerifiedProtocolRegionOwnership>(seal(std::move(summary)),"seal");
    auto built_control=BuildExceptionControl(body,graph,input,plan,{},&owner);
    auto control=Take<ExceptionControl>(std::move(built_control),"control");
    const auto verify=[&](const ExceptionControl& c) {
      return VerifyExceptionControl(body,graph,input,plan,c,{},&owner);
    };
    Take<std::uint64_t>(verify(control),"verify");
    test::Check(std::holds_alternative<ExceptionControlError>(VerifyExceptionControl(body,graph,input,plan,control)));
    const auto ordinary=BuildExceptionControl(body,graph,input,plan);
    test::Check(!std::holds_alternative<ExceptionControl>(ordinary));
    const auto refuse=[&](ExceptionControl value){test::Check(std::holds_alternative<ExceptionControlError>(verify(value)));};
    test::Check(!control.protocol_ports.empty());
    auto wrong=control;wrong.protocol_ports[0].pending.payload=CompletionEntryPayload{999};refuse(std::move(wrong));
    wrong=control;wrong.protocol_ports[0].pending.target=ExceptionNodeId{0};refuse(std::move(wrong));
    wrong=control;wrong.protocol_ports.pop_back();refuse(std::move(wrong));
    wrong=control;
    const auto exceptional=std::find_if(wrong.protocol_ports.begin(),wrong.protocol_ports.end(),[](const auto& p){
      return p.outcome.kind==CloseOutcomeKind::kGetterThrow || p.outcome.kind==CloseOutcomeKind::kCallThrow;
    });
    test::Check(exceptional!=wrong.protocol_ports.end());wrong.protocol_ports.erase(exceptional);refuse(std::move(wrong));
    wrong=control;wrong.region_roles[0]=ExceptionRegionRole::kInactive;refuse(std::move(wrong));
    wrong=control;wrong.inactive_phases.push_back({0});refuse(std::move(wrong));
    wrong=control;
    auto unit=std::find_if(wrong.units.begin(),wrong.units.end(),[](const auto& u){return std::holds_alternative<ProtocolCloseUnit>(u);});
    test::Check(unit!=wrong.units.end());std::get<ProtocolCloseUnit>(*unit).entry.successor^=1;refuse(std::move(wrong));
    const auto ordinary_region=std::find_if(control.regions.begin(),control.regions.end(),[](const auto& r){return r.has_value();});
    if(ordinary_region!=control.regions.end()) {
      wrong=control;wrong.regions[ordinary_region-control.regions.begin()]->handler=ordinary_region->value().body;refuse(std::move(wrong));
      wrong=control;wrong.regions[ordinary_region-control.regions.begin()].reset();refuse(std::move(wrong));
    }
    const SemanticBudget budget{};
    ControlQuota q(budget);auto topology=ProjectProtocolControl(graph,plan,input,owner,q);
    SemanticBudget used{budget.bytes-q.Remaining().bytes,budget.work-q.RemainingWork()};
    if(ordinary_region!=control.regions.end()) {
      // Delegated loop discovery releases its scratch before returning the
      // retained forest. Find the actual admission boundary, not retained size.
      std::size_t lo=used.bytes,hi=budget.bytes;
      while(lo<hi) {
        const auto mid=lo+(hi-lo)/2;bool admitted=false;
        try{ControlQuota trial({mid,used.work});ProjectProtocolControl(graph,plan,input,owner,trial);admitted=true;}
        catch(ControlError e){test::Check(e==ControlError::kResourceLimit);}
        catch(ExceptionControlError e){test::Check(e==ExceptionControlError::kResourceLimit);}
        if(admitted)hi=mid;else lo=mid+1;
      }
      used.bytes=lo;
    }
    ControlQuota exact(used);ProjectProtocolControl(graph,plan,input,owner,exact);
    for(bool bytes:{false,true}) {
      bool refused=false;auto small=used;
      if(bytes)--small.bytes;else --small.work;
      try {ControlQuota low(small);ProjectProtocolControl(graph,plan,input,owner,low);}
      catch(ControlError e){refused=e==(bytes?ControlError::kResourceLimit:ControlError::kWorkLimit);}
      catch(ExceptionControlError e){refused=e==(bytes?ExceptionControlError::kResourceLimit:ExceptionControlError::kWorkLimit);}
      test::Check(refused);
    }
    std::cout<<"padding="<<padding<<" units="<<control.units.size()<<" bytes="<<used.bytes<<" work="<<used.work<<'\n';
  }catch(const Refused&){test::Check(false);}
}
int main(int argc,char**argv) {
  test::Check(argc==2 || argc==3);
  const auto entity=argc==3?static_cast<unsigned>(std::stoul(argv[2])):451u;
  for(unsigned padding:{0,200,201,400,800})Case(argv[1],padding,entity);
}
