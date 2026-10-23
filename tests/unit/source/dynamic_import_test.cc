#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "abcd/abc/instruction.h"
#include "abcd/analysis/semantic_ir.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
void Check(bool ok, std::source_location at=std::source_location::current()) {
  if(!ok){std::cerr<<"dynamic import check "<<at.line()<<'\n';std::abort();}
}
ValueProgram Make(unsigned n, bool split) {
  ValueProgram g;
  g.values.push_back({EntryDefinition{ParameterEntry{0}},ValueKinds::Language(),{}});
  for(unsigned i=0;i<n;++i){
    const OpId id{i};const ValueId value{i+1};const UseId use{i};
    g.origins.push_back({{i},{i},{}});
    g.values.push_back({OpResult{id},ValueKinds::Defined(),{}});
    OperandUse occurrence{id,OperandRole::kInput,ReadPurpose::kOperand,ValueId{0},{},{},{},{}};
    occurrence.next=g.values[0].first_use;
    if(occurrence.next)g.uses[occurrence.next->value].previous=use;
    g.values[0].first_use=use;g.uses.push_back(occurrence);
    const auto effects=LanguageEffect::kAllocation|LanguageEffect::kUserCode|LanguageEffect::kModuleRead|LanguageEffect::kHeapRead|LanguageEffect::kHeapWrite;
    g.ops.push_back({ValueDynamicImport{}, {0},i,{i},1,value,effects,LanguageEffect::kAllocation,ExceptionalCompletion::kMayThrow,{i}});
  }
  g.origins.push_back({{n},{n},{}});
  const UseId returned{n};g.values[n].first_use=returned;
  g.uses.push_back({OpId{n},OperandRole::kReturnValue,ReadPurpose::kOperand,ValueId{n},{},{},{},{}});
  g.ops.push_back({ValueReturn{},{0},n,{n},1,{},LanguageEffect::kNone,LanguageEffect::kNone,ExceptionalCompletion::kImpossible,{n}});
  g.blocks={{{0},n+1,{0},0}};
  if(split){g.blocks.clear();for(unsigned i=0;i<g.ops.size();++i){
    g.ops[i].block={i};g.ops[i].order=0;const auto last=i+1==g.ops.size();
    g.blocks.push_back({{i},1,{i},static_cast<std::uint8_t>(last?0:1)});
    if(!last)g.edges.push_back({{i},{i+1},FlowEdgeKind::kFallthrough,0});
  }}
  return g;
}
void Proof(unsigned n,bool split) {
  const auto g=Make(n,split);
  ArraySourceInput input;input.contract=SourceContract::kLanguageSourceV10;
  const auto build=[&](SemanticBudget b={}){return BuildEvaluationPlan(g,b,IncomingCells::kReject,{},{},{},{},ClosureWrites::kUnknown,{},{},{},nullptr,{},input);};
  const auto evaluated=build();Check(std::holds_alternative<EvaluationPlan>(evaluated));
  const auto& e=std::get<EvaluationPlan>(evaluated);
  Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(g,e)));
  const auto c=BuildControlPlan(g);Check(std::holds_alternative<ControlPlan>(c));
  FunctionSyntax syntax{};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;
  syntax.identity.contract=input.contract;syntax.parameters.resize(1);
  std::vector<ExpressionConstant> values(g.ops.size());ExpressionConstants constants{values};
  const auto built=BuildFunctionTree(g,std::get<ControlPlan>(c),e,syntax,constants);
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree=std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(g,std::get<ControlPlan>(c),e,tree,constants)));
  const auto at=static_cast<std::size_t>(std::find_if(tree.expressions.begin(),tree.expressions.end(),[](const auto& x){return std::holds_alternative<DynamicImportExpr>(x.expression);})-tree.expressions.begin());
  Check(at<tree.expressions.size());
  auto wrong=tree;auto& source=std::get<DynamicImportExpr>(wrong.expressions[at].expression);
  source.specifier={static_cast<std::uint32_t>(at)};Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,std::get<ControlPlan>(c),e,wrong,constants)));
  wrong=tree;wrong.identity.contract=SourceContract::kLanguageSourceV9;
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,std::get<ControlPlan>(c),e,wrong,constants)));
  wrong=tree;wrong.expressions[at].expression=TreeLiteral{UndefinedLiteral{}};
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,std::get<ControlPlan>(c),e,wrong,constants)));
  for(bool work:{false,true}){
    std::size_t lo=0,hi=1U<<26;const auto run=[&](std::size_t x){SemanticBudget b;if(work)b.work=x;else b.bytes=x;return build(b);};
    while(lo<hi){const auto mid=lo+(hi-lo)/2;if(std::holds_alternative<EvaluationPlan>(run(mid)))hi=mid;else lo=mid+1;}
    Check(lo && std::holds_alternative<EvaluationPlan>(run(lo)));
    const auto failure=run(lo-1);Check(std::holds_alternative<EvaluationFailure>(failure));
    Check(std::get<EvaluationFailure>(failure).kind==(work?EvaluationError::kWorkLimit:EvaluationError::kResourceLimit));
    if(n==800)std::cout<<"dynamic 800 split="<<split<<' '<<(work?"work":"bytes")<<'='<<lo<<'\n';
  }
}
}
int main(){
  abc::DecodedInstruction decoded{};decoded.opcode=abc::Opcode::kDYNAMICIMPORT;
  const auto lifted=LiftSemanticInstruction({{0},decoded},abc::ProfileId::kDynamic2400,3);
  Check(std::holds_alternative<SemanticInstruction>(lifted));
  Check(std::holds_alternative<DynamicImport>(std::get<SemanticInstruction>(lifted).operation));
  Check(std::get<SemanticInstruction>(lifted).effects.normal_registers.read_count==1);
  decoded.operand_count=1;
  Check(std::get<SemanticFailure>(LiftSemanticInstruction({{0},decoded},abc::ProfileId::kDynamic2400,3)).kind==SemanticError::kMalformedInstruction);
  for(auto n:{1U,200U,201U,400U,800U})for(bool split:{false,true})Proof(n,split);}
