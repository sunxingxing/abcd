#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/recovery_flow.h"
#include "analysis/ssa_fixture.h"
#include "abcd/source/tree_printer.h"
#include <sstream>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Run(bool observe, bool outside = false) {
  const auto op=[](abc::Opcode opcode){return static_cast<std::uint8_t>(opcode);};
  // A fresh array and numeric cursor exist before the throwing operation.
  // Handler observation is an independent use, not a compiler-shape predicate.
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kCREATEEMPTYARRAY_IMM8),0,
      op(abc::Opcode::kSTA_V8),0,op(abc::Opcode::kLDAI_IMM32),0,0,0,0,
      op(abc::Opcode::kSTA_V8),1,op(abc::Opcode::kLDA_V8),2};
  const auto begin=static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(),{op(abc::Opcode::kSTARRAYSPREAD_V8_V8),0,1,
      op(abc::Opcode::kLDA_V8),0,op(abc::Opcode::kRETURN)});
  const auto handler=static_cast<std::uint32_t>(bytes.size());
  if(observe)bytes.insert(bytes.end(),{op(abc::Opcode::kLDA_V8),0});
  bytes.push_back(op(abc::Opcode::kRETURN));
  auto body=test::Body(bytes);
  abc::TryRegion region;region.offset={outside ? begin : 0};region.length=handler-region.offset.value;region.handler_count=1;
  abc::CatchHandler caught;caught.offset={handler};caught.length=bytes.size()-handler;
  body.tries.push_back(region);body.handlers.push_back(caught);
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(2,1,{}));
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,abc::ProfileId::kDynamic1206,frame.registers));
  IdfQuota ssa_work{kDefaultSemanticWork};
  const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,ssa_work));
  std::vector<EntryValueFact> entries;
  for(std::size_t i=0;i<ssa.Values().size();++i) {
    const auto& value=ssa.Values()[i];const auto* reg=std::get_if<abc::RegisterId>(&value.slot);
    if(reg&&reg->value>=frame.locals&&std::holds_alternative<MethodEntry>(value.origin))
      entries.push_back({{static_cast<std::uint32_t>(i)},ValueKinds::Language()});
  }
  const auto env=std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entries,value_work,{},PendingExceptionValues::kLanguage));
  const auto proof=std::get<ExceptionInputProof>(VerifyExceptionInputs(flow,ssa,env,values));
  const PhasedValueInput input{flow,ssa,env,values,proof,frame};
  const auto graph=std::get<ValueProgram>(ImportPhasedValues(input));
  std::vector<ExpressionConstant> constants(graph.ops.size());
  std::vector<std::u16string> names{u"source"};
  const ExpressionConstants resolved{constants,names.size()};
  std::optional<ArrayInitializationError> cause;
  const auto result=BuildPhasedEvaluationPlan(graph,{},IncomingCells::kReject,{},{},{},{},ClosureWrites::kUnknown,
      {SourceContract::kLanguageSourceV1,resolved,names,&cause});
  if(observe) {
    test::Check(cause==ArrayInitializationError::kEscape||cause==ArrayInitializationError::kCatch);
    test::Check(std::holds_alternative<EvaluationError>(result));return;
  }
  if(cause)std::cerr<<"array cause "<<unsigned(*cause)<<'\n';
  if(const auto* error=std::get_if<EvaluationError>(&result))std::cerr<<"eval "<<unsigned(*error)<<'\n';
  test::Check(std::holds_alternative<EvaluationPlan>(result));
  const auto& evaluation=std::get<EvaluationPlan>(result);
  const auto regions=std::get<ExceptionPlan>(BuildExceptionPlan(body,graph,input));
  const auto control=std::get<ExceptionControl>(BuildExceptionControl(body,graph,input,regions));
  const ExceptionTreeInput context{body,input,regions,control};
  FunctionSyntax syntax{};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;
  syntax.identity.contract=SourceContract::kLanguageSourceV1;syntax.names=names;
  syntax.parameters={{NameId{0},NameEvidence::kDebugRange}};
  auto built=BuildExceptionFunctionDraft(graph,context,evaluation,syntax,resolved);
  if(outside) { test::Check(std::holds_alternative<FunctionTreeFailure>(built)); return; }
  if(const auto* error=std::get_if<FunctionTreeFailure>(&built))std::cerr<<"tree "<<unsigned(error->kind)<<"\n";
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  const auto& tree=std::get<ExceptionFunctionDraft>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph,context,evaluation,tree,resolved)));
  auto changed=tree;
  for(auto& node:changed.expressions)if(auto* element=std::get_if<ArrayElement>(&node.expression))
    if(auto* spread=std::get_if<ArraySpreadElement>(&element->element)) {element->element=ArrayValueElement{spread->iterable};break;}
  test::Check(std::holds_alternative<TreeSemanticError>(VerifyExceptionTreeSemantics(graph,context,evaluation,changed,resolved)));
}
int main(){Run(false);Run(true);Run(false,true);}
