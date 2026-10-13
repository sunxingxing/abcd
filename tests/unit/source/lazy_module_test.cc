#include "../../../src/source/function_values.h"
#include "../../../src/source/module_guards.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/analysis/phased_value_import.h"
#include "abcd/analysis/exception_modules.h"
#include "analysis/ssa_fixture.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"

using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
abc::MethodBody Body(std::uint32_t count, bool wide, bool caught) {
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode opcode) {
    const auto word=static_cast<std::uint16_t>(opcode);
    bytes.push_back(word & UINT8_MAX); if(word>UINT8_MAX)bytes.push_back(word>>8);
  };
  for(std::uint32_t i=0;i<count;++i) {
    emit(wide?abc::Opcode::kCALLRUNTIME_WIDELDLAZYMODULEVAR_PREF_IMM16:abc::Opcode::kCALLRUNTIME_LDLAZYMODULEVAR_PREF_IMM8);
    bytes.push_back(7);if(wide)bytes.push_back(0);
    emit(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);bytes.insert(bytes.end(),{3,0});
  }
  const auto end=static_cast<std::uint32_t>(bytes.size());
  emit(abc::Opcode::kRETURN);
  if(caught)emit(abc::Opcode::kRETURN);
  abc::MethodBody body;
  for(std::uint32_t pc=0;pc<bytes.size();) {
    const auto decoded=std::get<abc::DecodedInstruction>(abc::DecodeInstruction(std::span(bytes).subspan(pc),abc::ProfileId::kDynamic2400));
    body.instructions.push_back({{pc},decoded,{}});pc+=decoded.size;
  }
  if(caught) {
    abc::TryRegion region;region.length=end;region.handler_count=1;body.tries.push_back(region);
    abc::CatchHandler handler;handler.offset={end+1};handler.length=1;body.handlers.push_back(handler);
  }
  return body;
}
void Phased(const abc::MethodBody& body,std::uint32_t count) {
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,0,{}));
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,abc::ProfileId::kDynamic2400,0));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,0,quota));
  const auto modules=std::get<ExceptionModules>(ProveExceptionModules(flow,ssa));
  test::Check(modules.reads.size()==count);
  // Exceptions leave Before, never the success-only accumulator Commit.
  for(std::size_t i=0;i<flow.nodes.size();++i) {
    const auto& node=flow.nodes[i];
    if(node.phase==ExceptionPhase::kCommit)test::Check(!node.exception);
    if(node.exception)test::Check(node.phase==ExceptionPhase::kBefore);
  }
  const auto environments=std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota work{kDefaultSemanticWork};
  const auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,{},work,{},PendingExceptionValues::kLanguage,modules.loads));
  const auto proof=std::get<ExceptionInputProof>(VerifyExceptionInputs(flow,ssa,environments,values));
  const PhasedValueInput input{flow,ssa,environments,values,proof,frame};
  const auto graph=std::get<ValueProgram>(ImportPhasedValues(input));
  const auto guards=std::get<ModuleGuards>(BuildPhasedModuleGuards(input,graph));
  std::vector<ModuleEvaluation> bindings;
  for(const auto& pair:guards.pairs) {
    const auto read=std::get<ValueModuleRead>(graph.ops[pair.read.value].operation);
    test::Check(read.loading==ModuleLoading::kLazy);
    bindings.push_back({pair,read,{{1},{0}}});
  }
  const auto plan=std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph,{},IncomingCells::kReject,{},bindings));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph,plan)));
  if(count==1) {
    const auto regions=std::get<ExceptionPlan>(BuildExceptionPlan(body,graph,input));
    const auto control=std::get<ExceptionControl>(BuildExceptionControl(body,graph,input,regions));
    FunctionSyntax syntax{};syntax.identity.contract=SourceContract::kLanguageSourceV1;
    syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;syntax.names={u"value"};
    syntax.modules.push_back({{{1},{0}},{0},ModuleAccess::kReadOnly,NameEvidence::kSynthesized});
    std::vector<ExpressionConstant> constants(graph.ops.size());
    const ExceptionTreeInput exceptions{body,input,regions,control};
    const auto draft=std::get<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(graph,exceptions,plan,syntax,{constants,1}));
    auto forged_tree=draft.tree;
    bool changed=false;
    for(auto& node:forged_tree.expressions)if(auto* read=std::get_if<SymbolExpr>(&node.expression);read&&read->loading==ModuleLoading::kLazy) {
      read->loading=ModuleLoading::kOrdinary;changed=true;break;
    }
    test::Check(changed);
    test::Check(std::holds_alternative<TreeSemanticError>(VerifyExceptionTreeSemantics(graph,exceptions,plan,forged_tree,{constants,1})));
  }
  auto forged=graph;
  std::get<ValueModuleRead>(forged.ops[guards.pairs[0].read.value].operation).loading=ModuleLoading::kOrdinary;
  test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(forged,plan)));
  auto omitted=plan;omitted.operations[guards.pairs[0].guard->value].kind=EvaluationKind::kRemoved;
  test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph,omitted)));
}
}
int main() {
  for(const auto count:{1U,200U,201U,400U,800U})for(const bool wide:{false,true}) {
    const auto body=Body(count,wide,false);
    RouteInputIdentity identity;identity.profile=abc::ProfileId::kDynamic2400;identity.contract=SourceContract::kLanguageSourceV1;
    const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,0,{}));
    const auto input=std::get<FunctionInput>(FunctionInput::Build(identity,{300},{100},body,frame,InputCaptureMode::kClosed));
    const auto values=std::get<FunctionValues>(BuildFunctionValues(input,body,frame));
    test::Check(values.modules.pairs.size()==count);
    auto missing=values.modules;missing.pairs.clear();
    test::Check(std::get<ModuleGuardError>(VerifyLazyModuleReads(values.graph,missing,{}))==ModuleGuardError::kReadProof);
    auto forged=values.graph;
    std::get<ValueModuleRead>(forged.ops[0].operation).loading=ModuleLoading::kOrdinary;
    test::Check(std::holds_alternative<ModuleGuardError>(VerifyModuleGuards(input,forged,values.modules)));
    Phased(body,count);Phased(Body(count,wide,true),count);
  }
}
