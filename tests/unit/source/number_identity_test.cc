#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/source_variables.h"
#include "../../support/analysis/ssa_fixture.h"
#include <iostream>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
namespace {
template<class T,class V>T Take(V value) {
  Check(std::holds_alternative<T>(value));return std::get<T>(std::move(value));
}
void Case(unsigned count) {
  std::vector<std::uint8_t> bytes;
  for(unsigned i=0;i<count;++i) {
    bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kLDAI_IMM32));
    for(unsigned shift=0;shift<32;shift+=8)bytes.push_back((i>>shift)&255);
    bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kTONUMERIC_IMM8));bytes.push_back(0);
    bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kSTA_V8));bytes.push_back(0);
  }
  bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
  const auto body=test::Body(bytes);
  const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(1,0,{}));
  RouteInputIdentity identity;identity.profile=abc::ProfileId::kDynamic1206;identity.contract=SourceContract::kLanguageSourceV5;
  const auto input=Take<FunctionInput>(FunctionInput::Build(identity,{10},{20},body,frame,InputCaptureMode::kClosed));
  const auto values=Take<FunctionValues>(BuildFunctionValues(input,body,frame));const auto& graph=values.graph;
  const auto control=Take<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation=Take<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{};syntax.identity=identity;syntax.method={20};syntax.owner={0};
  syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;
  std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants resolved{constants,0,0};
  const auto build=[&](SemanticBudget budget={}){return BuildFunctionTree(graph,control,evaluation,syntax,resolved,budget);};
  const auto made=Take<ConstructedFunction>(build());Check(made.flow&&made.flow->original);
  const auto& tree=made.tree;const auto& original=*made.flow->original;
  std::vector<std::optional<SymbolId>> aliases(tree.symbols.size());
  for(unsigned i=0;i<aliases.size();++i)aliases[i]=SymbolId{i};
  std::vector<std::optional<PrimitiveId>> literals(tree.symbols.size());
  std::vector<std::optional<ExprId>> replacements(tree.expressions.size());
  std::vector<std::uint8_t> omitted(tree.statements.size()),normal(tree.expressions.size());
  std::vector<ExpressionEvaluation> effects(tree.expressions.size(),ExpressionEvaluation::kRequired);
  LegacyFlowProjection projection{aliases,literals,replacements,omitted,effects,normal};projection.original=&original;
  auto flow=Take<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  std::size_t found=0;
  for(unsigned o=0;o<original.operations.size();++o)if(original.operations[o].number_identity) {
    ++found;const auto proof=FindFlowOriginalNumberIdentity(flow,OpId{o});Check(proof.has_value());
    auto bad=original;bad.operations[o].number_identity->input={UINT32_MAX};
    auto changed=flow;changed.original=&bad;Check(!FindFlowOriginalNumberIdentity(changed,OpId{o}));
    bad=original;bad.operations[o].number_identity->expression={UINT32_MAX};
    Check(!FindFlowOriginalNumberIdentity(changed,OpId{o}));
    bad=original;bad.uses[bad.operations[o].first_use.value].operand=SpecialValue::kUndefined;
    Check(!FindFlowOriginalNumberIdentity(changed,OpId{o}));
    bad=original;bad.uses[bad.operations[o].first_use.value].owner=ValueId{0};
    Check(!FindFlowOriginalNumberIdentity(changed,OpId{o}));
    changed=flow;changed.normal_exits[proof->expression.value].reset();
    const auto dead=FindFlowOriginalNumberIdentity(changed,OpId{o});Check(dead && !dead->input);
  }
  Check(found==count);
  Take<ConstructedFunction>(build({SemanticBudget{}.bytes,made.work}));
  Check(std::get<FunctionTreeFailure>(build({SemanticBudget{}.bytes,made.work-1})).kind==FunctionTreeError::kWorkLimit);
  std::size_t low=0,high=SemanticBudget{}.bytes;
  while(low<high){const auto mid=low+(high-low)/2;const auto result=build({mid,SemanticBudget{}.work});
    if(std::holds_alternative<ConstructedFunction>(result))high=mid;
    else {Check(std::get<FunctionTreeFailure>(result).kind==FunctionTreeError::kResourceLimit);low=mid+1;}}
  Take<ConstructedFunction>(build({low,SemanticBudget{}.work}));
  Check(low&&std::get<FunctionTreeFailure>(build({low-1,SemanticBudget{}.work})).kind==FunctionTreeError::kResourceLimit);
  std::cout<<"identities="<<found<<" work="<<made.work<<" bytes="<<low<<'\n';
}
}
int main(){for(const unsigned n:{1U,200U,201U,400U,800U})Case(n);}
