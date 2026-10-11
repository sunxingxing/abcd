#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/exception_tree.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include "source/module_units.h"
#include <sstream>
using namespace abcd;using namespace abcd::analysis;using namespace abcd::source;
namespace {
void Run(unsigned count,abc::ProfileId profile,bool emit) {
  const auto opcode=[](abc::Opcode op){return static_cast<std::uint8_t>(op);};
  std::vector<std::uint8_t> bytes;
  for(unsigned i=0;i<count;++i)bytes.insert(bytes.end(),{
      opcode(abc::Opcode::kLDA_V8),0,opcode(abc::Opcode::kCALLARG0_IMM8),0,
      opcode(abc::Opcode::kSTMODULEVAR_IMM8),0});
  bytes.push_back(opcode(abc::Opcode::kRETURNUNDEFINED));const auto handler=static_cast<unsigned>(bytes.size());
  bytes.push_back(opcode(abc::Opcode::kRETURNUNDEFINED));
  auto body=test::Body(bytes);abc::TryRegion region;region.offset={0};region.length=handler-1;region.handler_count=1;
  body.tries.push_back(region);abc::CatchHandler caught;caught.offset={handler};caught.length=1;body.handlers.push_back(caught);
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,1,{false,false,false,false}));
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,profile,frame.registers));
  IdfQuota quota{kDefaultSemanticWork};const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,quota));
  std::vector<EntryValueFact> entries;
  for(unsigned i=0;i<ssa.Values().size();++i)if(std::holds_alternative<MethodEntry>(ssa.Values()[i].origin))
    if(std::holds_alternative<abc::RegisterId>(ssa.Values()[i].slot))entries.push_back({{i},ValueKinds::Language()});
  const auto env=std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  // The independent JS fixture supplies language-valued callbacks and throws;
  // their owner premises must be explicit before phased input verification.
  IdfQuota work{kDefaultSemanticWork};const auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(
      flow,ssa,entries,work,{},PendingExceptionValues::kLanguage,{},{},CallResultValues::kLanguage));
  const auto proof=std::get<ExceptionInputProof>(VerifyExceptionInputs(flow,ssa,env,values));
  const PhasedValueInput input{flow,ssa,env,values,proof,frame};const auto graph=std::get<ValueProgram>(ImportPhasedValues(input));
  const auto evaluation=std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  const auto regions=std::get<ExceptionPlan>(BuildExceptionPlan(body,graph,input));
  const auto control=std::get<ExceptionControl>(BuildExceptionControl(body,graph,input,regions));
  FunctionSyntax syntax{};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;syntax.parameters.resize(1);
  syntax.names={u"exported",u"other"};
  syntax.modules={{{{7},{0}},{0},ModuleAccess::kMutable,NameEvidence::kMetadata},
                  {{{7},{1}},{1},ModuleAccess::kMutable,NameEvidence::kMetadata}};
  std::vector<ExpressionConstant> operations(graph.ops.size());std::vector<OpId> stores;
  for(unsigned i=0;i<graph.ops.size();++i)if(const auto* store=std::get_if<ValueModuleWrite>(&graph.ops[i].operation)) {
    operations[i]=ModuleStoreConstant{store->slot,{{7},{0}},false};stores.push_back({i});
  }
  const ExceptionTreeInput context{body,input,regions,control};
  const auto draft=std::get<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(graph,context,evaluation,syntax,{operations,syntax.names.size()}));
  test::Check(draft.flow&&draft.flow->original);const auto& original=*draft.flow->original;
  const auto cost=test::CheckModuleWrites(draft.tree,original,stores,draft.tree.modules[0].symbol,draft.tree.modules[1].symbol);
  if(!emit)std::cout<<count<<' '<<static_cast<unsigned>(profile)<<' '<<cost.work<<' '<<cost.bytes<<'\n';
  if(emit) {
    std::ostringstream output;const auto printed=PrintFunctionTree(draft.tree,output,{}, {},nullptr,&original);
    test::Check(std::holds_alternative<PrintedFunction>(printed));std::cout<<output.str();
  }
}
}
int main(int argc,char**) {
  if(argc>1){Run(3,abc::ProfileId::kDynamic1206,true);return 0;}
  for(const auto profile:{abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301})
    for(unsigned count:{1U,200U,201U,400U,800U})Run(count,profile,false);
}
