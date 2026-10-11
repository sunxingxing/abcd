#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/exception_tree.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include "source/input_actions.h"
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;using namespace abcd::analysis;using namespace abcd::source;
namespace {
enum class Input {kReceiver,kSelf,kRealm,kSymbol,kSnapshot,kGlobal};
void Run(Input kind,abc::ProfileId profile,unsigned copies,bool emit) {
  const auto op=[](abc::Opcode code){return static_cast<std::uint8_t>(code);};
  constexpr std::uint8_t kSaved=0,kImplicitArguments=3,kCallback=1+kImplicitArguments;
  std::vector<std::uint8_t> bytes;
  switch(kind) {
    case Input::kReceiver:bytes.push_back(op(abc::Opcode::kLDTHIS));break;
    case Input::kSelf:bytes.push_back(op(abc::Opcode::kLDFUNCTION));break;
    case Input::kRealm:bytes.push_back(op(abc::Opcode::kLDGLOBAL));break;
    case Input::kSymbol:bytes.push_back(op(abc::Opcode::kLDSYMBOL));break;
    case Input::kSnapshot:bytes.push_back(op(abc::Opcode::kGETUNMAPPEDARGS));break;
    case Input::kGlobal:bytes.insert(bytes.end(),{op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16),0,0,0});break;
  }
  for(unsigned i=0;i<copies;++i)bytes.insert(bytes.end(),{op(abc::Opcode::kSTA_V8),kSaved,op(abc::Opcode::kLDA_V8),kSaved});
  bytes.insert(bytes.end(),{op(abc::Opcode::kSTA_V8),kSaved,op(abc::Opcode::kLDA_V8),kCallback,
      op(abc::Opcode::kCALLARG0_IMM8),0,op(abc::Opcode::kLDA_V8),kSaved,op(abc::Opcode::kRETURN)});
  const auto handler=static_cast<unsigned>(bytes.size());bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
  auto body=test::Body(bytes);abc::TryRegion region;region.offset={0};region.length=handler;region.handler_count=1;
  body.tries.push_back(region);abc::CatchHandler caught;caught.offset={handler};caught.length=1;body.handlers.push_back(caught);
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(1,kImplicitArguments+1,{.this_slot=true,.new_target_slot=true,.extra_arguments=true,.function_slot=true}));
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,profile,frame.registers,{}, {},&frame));
  IdfQuota quota{kDefaultSemanticWork};const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,quota));
  std::vector<EntryValueFact> entries;
  for(unsigned i=0;i<ssa.Values().size();++i)if(std::holds_alternative<MethodEntry>(ssa.Values()[i].origin))
    if(const auto* reg=std::get_if<abc::RegisterId>(&ssa.Values()[i].slot);reg&&reg->value>=frame.locals)
      entries.push_back({{i},ValueKinds::Language()});
  const auto env=std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota work{kDefaultSemanticWork};const auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(
      flow,ssa,entries,work,{},PendingExceptionValues::kLanguage,{},{},CallResultValues::kLanguage,FrameReceiver::kInitialized));
  const ArgumentFrameFact invocation{frame};
  const auto proof=std::get<ExceptionInputProof>(VerifyExceptionInputs(flow,ssa,env,values,kDefaultSemanticWork,&invocation));
  const PhasedValueInput input{flow,ssa,env,values,proof,frame};const auto graph=std::get<ValueProgram>(ImportPhasedValues(input));
  const auto evaluation=std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  const auto regions=std::get<ExceptionPlan>(BuildExceptionPlan(body,graph,input));
  const auto control=std::get<ExceptionControl>(BuildExceptionControl(body,graph,input,regions));
  FunctionSyntax syntax{};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;syntax.parameters.resize(1);
  syntax.names={u"holder",u"probe"};syntax.inferred_name=NameId{0};syntax.identity.profile=profile;
  std::vector<ExpressionConstant> operations(graph.ops.size());
  for(unsigned i=0;i<graph.ops.size();++i)if(std::holds_alternative<ValueGlobalRead>(graph.ops[i].operation))operations[i]=NameId{1};
  const ExceptionTreeInput context{body,input,regions,control};
  const auto draft=std::get<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(graph,context,evaluation,syntax,{operations,syntax.names.size()}));
  test::Check(draft.flow&&draft.flow->original);const auto& original=*draft.flow->original;
  const auto cost=test::CheckInputActions(draft.tree,original);
  if(!emit)std::cout<<static_cast<unsigned>(profile)<<' '<<static_cast<unsigned>(kind)<<' '<<copies<<' '<<cost.work<<' '<<cost.bytes<<'\n';
  if(emit) {
    const HostRealmBinding realm{u"hostRealm"};const HostSymbolBinding symbol{u"hostSymbol"};
    const HostArgumentSnapshotBinding snapshot{u"snapshotArguments",ArgumentSnapshotAbi::kV7CurrentSlots};
    GlobalScope scope{};scope.realm_binding=&realm;scope.symbol_binding=&symbol;scope.argument_snapshot_binding=&snapshot;
    std::ostringstream output;test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(draft.tree,output,{}, {},&scope,&original)));
    std::cout<<nlohmann::json{{"kind",static_cast<unsigned>(kind)},{"copies",copies},{"source",output.str()}}<<'\n';
  }
}
}
int main(int argc,char**) {
  for(const auto profile:{abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301})
    for(const auto kind:{Input::kReceiver,Input::kSelf,Input::kRealm,Input::kSymbol,Input::kSnapshot,Input::kGlobal})
      for(unsigned copies:argc>1?std::initializer_list<unsigned>{0,2}:std::initializer_list<unsigned>{0,2,200,201,400,800})
        Run(kind,profile,copies,argc>1);
}
