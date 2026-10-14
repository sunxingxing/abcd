#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/analysis/value_flow.h"
#include "../../support/analysis/ssa_fixture.h"
using namespace abcd;
using namespace abcd::analysis;
using test::Check;
void CheckPhases(std::initializer_list<std::uint8_t> code, bool defines_acc, bool boolean) {
 auto body=test::Body(code);
 const auto handler_pc=body.instructions.back().offset.value+1;
 body.instructions.push_back(test::Body({0x64}).instructions[0]);body.instructions.back().offset={handler_pc};
 abc::TryRegion region;region.length=handler_pc-1;region.handler_count=1;body.tries.push_back(region);
 abc::CatchHandler handler;handler.offset={handler_pc};handler.length=1;body.handlers.push_back(handler);
 const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,3,{}));
 const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,abc::ProfileId::kDynamic1301,frame.registers,{}, {}, &frame));
 IdfQuota quota{kDefaultSemanticWork};
 const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,quota));
 std::vector<EntryValueFact> entries;
 for(std::size_t i=0;i<ssa.Values().size();++i)
  if(std::holds_alternative<MethodEntry>(ssa.Values()[i].origin)&&std::holds_alternative<abc::RegisterId>(ssa.Values()[i].slot))
   entries.push_back({{static_cast<std::uint32_t>(i)},ValueKinds::Language()});
 const auto env=std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
 const auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entries,quota,{},PendingExceptionValues::kLanguage));
 const auto proof=std::get<ExceptionInputProof>(VerifyExceptionInputs(flow,ssa,env,values));
 const PhasedValueInput input{flow,ssa,env,values,proof,frame};
 const auto graph=std::get<ValueProgram>(ImportPhasedValues(input));
 Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph,input)));
 const auto& op=graph.ops[1];
 Check(op.result.has_value()==defines_acc&&op.exceptional==ExceptionalCompletion::kMayThrow);
 Check(op.normal==op.before_throw);
 if(defines_acc){
  const auto result=std::get<OpResult>(graph.values[op.result->value].origin);
  Check(result.commit&&graph.phases[result.commit->value].phase==ExceptionPhase::kCommit);
  Check(!boolean||graph.values[op.result->value].domain.IsBoolean());
  auto wrong=graph;std::get<OpResult>(wrong.values[op.result->value].origin).commit.reset();
  Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(wrong,input)));
 }
 Check(graph.exceptional_exits.size()==1&&graph.exceptional_exits[0].handler);
 auto wrong=graph;wrong.exceptional_exits[0].handler.reset();
 Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(wrong,input)));
 wrong=graph;wrong.ops[1].before_throw=LanguageEffect::kNone;
 Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(wrong,input)));
}
int main(){
 CheckPhases({0x60,1,0xc2,0,0x64},true,true);
 CheckPhases({0x60,1,0xfb,2,0,7,0,0,0,0,0x64},false,false);
 CheckPhases({0x60,1,0x46,0,0,0,0x64},true,false);
}
