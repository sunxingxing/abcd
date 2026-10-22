#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"
#include "../../../src/source/function_abi.h"
#include "../../../src/analysis/value_operands.h"
#include "abcd/analysis/semantic_ir.h"
#include "analysis/ssa_fixture.h"
#include <iostream>
using namespace abcd;using namespace abcd::source;using namespace abcd::analysis;using test::Check;
ValueProgram Graph(std::uint32_t n) {
  ValueProgram g;
  for(std::uint32_t i=0;i<n;++i){g.values.push_back({OpResult{{i}},ValueKinds::Language(),{}});g.origins.push_back({{i},{i},{}});g.ops.push_back({ValueNewTarget{},{0},i,{0},0,ValueId{i},LanguageEffect::kArgumentsRead,LanguageEffect::kNone,ExceptionalCompletion::kImpossible,{i}});}
  g.values.back().first_use=UseId{0};g.uses.push_back({OpId{n},OperandRole::kReturnValue,ReadPurpose::kOperand,ValueId{n-1},{},{},{},{}});
  g.origins.push_back({{n},{n},{}});g.ops.push_back({ValueReturn{},{0},n,{0},1,{},LanguageEffect::kNone,LanguageEffect::kNone,ExceptionalCompletion::kImpossible,{n}});g.blocks={{{0},n+1,{0},0}};return g;
}
int main(){
  abc::DecodedInstruction d{};d.opcode=abc::Opcode::kLDNEWTARGET;
  auto lifted=std::get<SemanticInstruction>(LiftSemanticInstruction({{0},d},abc::ProfileId::kDynamic1206,0));
  Check(std::holds_alternative<NewTargetLoad>(lifted.operation)&&lifted.effects.normal==LanguageEffect::kArgumentsRead);
  Check(std::holds_alternative<ValueNewTarget>(ImportValueOperation(lifted.operation)));
  d.operand_count=1;Check(std::get<SemanticFailure>(LiftSemanticInstruction({{0},d},abc::ProfileId::kDynamic1206,0)).kind==SemanticError::kMalformedInstruction);
  for(auto n:{1U,200U,201U,400U,800U}){
    auto g=Graph(n);auto eval=std::get<EvaluationPlan>(BuildEvaluationPlan(g));auto control=std::get<ControlPlan>(BuildControlPlan(g));
    FunctionSyntax s{};s.identity.contract=SourceContract::kLanguageSourceV7;s.callable=CallableKind::kFunction;s.strictness=Strictness::kStrict;
    std::vector<ExpressionConstant> constants(g.ops.size());ExpressionConstants c{constants,0,0};
    auto run=[&](SemanticBudget b){return BuildFunctionTree(g,control,eval,s,c,b);};
    const auto good=std::get<ConstructedFunction>(run({}));
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(g,control,eval,good.tree,c)));
    const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,0,{}));
    const auto abi=std::get<ConstructedAbi>(BuildFunctionAbi(g,good.tree,frame,0));Check(abi.abi.new_target==AbiObservation::kFrameIdentity);
    auto bad=good.tree;bad.identity.contract=SourceContract::kLanguageSourceV6;
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,control,eval,bad,c)));
    Check(std::holds_alternative<TreeStructureError>(VerifyTreeStructure(bad)));
    for(auto kind:{CallableKind::kArrow,CallableKind::kAsyncArrow}){bad=good.tree;bad.callable=kind;Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,control,eval,bad,c)));}
    bad=good.tree;std::get<NewTargetExpr>(bad.expressions[0].expression).owner.value++;
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,control,eval,bad,c)));
    bad=good.tree;std::get<NewTargetExpr>(bad.expressions[0].expression).form=NewTargetForm::kAbiEntry;
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,control,eval,bad,c)));
    SemanticBudget b;b.work=good.work;Check(std::holds_alternative<ConstructedFunction>(run(b)));--b.work;Check(std::get<FunctionTreeFailure>(run(b)).kind==FunctionTreeError::kWorkLimit);
    std::size_t lo=0,hi=SemanticBudget{}.bytes;while(lo<hi){auto m=lo+(hi-lo)/2;b={};b.bytes=m;if(std::holds_alternative<ConstructedFunction>(run(b)))hi=m;else lo=m+1;}
    b={};b.bytes=lo;Check(std::holds_alternative<ConstructedFunction>(run(b)));--b.bytes;Check(std::get<FunctionTreeFailure>(run(b)).kind==FunctionTreeError::kResourceLimit);
    std::cout<<n<<" "<<lo<<" bytes "<<good.work<<" work\n";
  }
}
