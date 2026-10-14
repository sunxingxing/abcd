#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../support/analysis/ssa_fixture.h"
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
using test::Check;
ValueProgram Graph(std::uint32_t count) {
  ValueProgram g;
  for(std::uint32_t i=0;i<2;++i)g.values.push_back({EntryDefinition{ParameterEntry{i}},ValueKinds::Language(),{}});
  const auto use=[&](OpId op,OperandRole role,ValueId input){
    const UseId id{static_cast<std::uint32_t>(g.uses.size())};
    OperandUse use{op,role,ReadPurpose::kOperand,input,{},{},{},{}};
    use.next=g.values[input.value].first_use;
    if(use.next)g.uses[use.next->value].previous=id;
    g.values[input.value].first_use=id;g.uses.push_back(use);
  };
  for(std::uint32_t i=0;i<=count;++i){
    const OpId id{i};const bool returned=i==count;
    std::optional<ValueId> result;
    if(!returned){result=ValueId{i+2};g.values.push_back({OpResult{id},ValueKinds::Boolean(),{}});}
    g.origins.push_back({{i},{i},{}});
    const auto effects=LanguageEffect::kHeapRead|LanguageEffect::kHeapWrite|LanguageEffect::kUserCode|LanguageEffect::kAllocation;
    g.ops.push_back({returned?ValueOperation{ValueReturn{}}:ValueOperation{ValuePropertyDelete{}},
        {0},i,{static_cast<std::uint32_t>(g.uses.size())},returned?1U:2U,result,
        returned?LanguageEffect::kNone:effects,returned?LanguageEffect::kNone:effects,
        returned?ExceptionalCompletion::kImpossible:ExceptionalCompletion::kMayThrow,{i}});
    if(returned)use(id,OperandRole::kReturnValue,{count+1});
    else {use(id,OperandRole::kBase,{0});use(id,OperandRole::kKey,{1});}
  }
  g.blocks={{{0},count+1,{0},0}};return g;
}
int main(){
 for(const auto n:{1U,200U,201U,400U,800U}) for (const bool split : {false, true}) {
  auto graph=Graph(n);
  if (split) {
   graph.blocks.clear(); graph.edges.clear();
   for (std::uint32_t i=0;i<graph.ops.size();++i) {
    graph.ops[i].block={i};graph.ops[i].order=0;
    const bool last=i+1==graph.ops.size();
    graph.blocks.push_back({{i},1,{static_cast<std::uint32_t>(graph.edges.size())},static_cast<std::uint8_t>(last?0:1)});
    if(!last)graph.edges.push_back({{i},{i+1},FlowEdgeKind::kFallthrough,0});
   }
  }
  const auto evaluation=std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  const auto control=std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;syntax.parameters.resize(2);
  const std::vector<ExpressionConstant> constants(graph.ops.size());const ExpressionConstants resolved{constants,0,0};
  const auto constructed=std::get<ConstructedFunction>(BuildFunctionTree(graph,control,evaluation,syntax,resolved));
  const auto& tree=constructed.tree;
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,evaluation,tree,resolved)));
  auto wrong=tree;wrong.strictness=Strictness::kSloppy;
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph,control,evaluation,wrong,resolved)));
  wrong=tree;
  for(auto& node:wrong.expressions)if(auto* deleted=std::get_if<DeleteExpr>(&node.expression)){
    node.expression=MemberExpr{deleted->base,deleted->key};break;
  }
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph,control,evaluation,wrong,resolved)));
  wrong=tree;
  for(auto& node:wrong.expressions)if(auto* deleted=std::get_if<DeleteExpr>(&node.expression)){
    std::swap(deleted->base,std::get<ComputedKey>(deleted->key).expression);break;
  }
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph,control,evaluation,wrong,resolved)));
  SemanticBudget budget;budget.work=constructed.work;
  Check(std::holds_alternative<ConstructedFunction>(BuildFunctionTree(graph,control,evaluation,syntax,resolved,budget)));
  --budget.work;
  Check(std::get<FunctionTreeFailure>(BuildFunctionTree(graph,control,evaluation,syntax,resolved,budget)).kind==FunctionTreeError::kWorkLimit);
  std::size_t low=0,high=SemanticBudget{}.bytes;
  while(low<high){const auto middle=low+(high-low)/2;budget={};budget.bytes=middle;
    if(std::holds_alternative<ConstructedFunction>(BuildFunctionTree(graph,control,evaluation,syntax,resolved,budget)))high=middle;else low=middle+1;
  }
  budget={};budget.bytes=low;
  Check(std::holds_alternative<ConstructedFunction>(BuildFunctionTree(graph,control,evaluation,syntax,resolved,budget)));
  --budget.bytes;
  Check(std::get<FunctionTreeFailure>(BuildFunctionTree(graph,control,evaluation,syntax,resolved,budget)).kind==FunctionTreeError::kResourceLimit);
 }
}
