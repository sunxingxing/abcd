#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "abcd/analysis/named_read.h"
#include "../../support/analysis/ssa_fixture.h"
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
using test::Check;
ValueProgram Graph(std::uint32_t count) {
  ValueProgram g;
  g.values.push_back({EntryDefinition{ReceiverEntry{}}, ValueKinds::Language(), {}});
  for (std::uint32_t i = 0; i <= count; ++i) {
    const OpId id{i}; const bool returned = i == count;
    const ValueId input{returned ? count : 0};
    std::optional<ValueId> result;
    if (!returned) { result = ValueId{i+1}; g.values.push_back({OpResult{id},ValueKinds::Language(),{}}); }
    g.origins.push_back({{i},{i},{}});
    g.ops.push_back({returned ? ValueOperation{ValueReturn{}} : ValueOperation{ValueNamedRead{{0},PropertyLookup::kSuper}},
        {0},i,{i},1,result,returned?LanguageEffect::kNone:LanguageEffect::kHeapRead|LanguageEffect::kUserCode,
        returned?LanguageEffect::kNone:LanguageEffect::kHeapRead|LanguageEffect::kUserCode,
        returned?ExceptionalCompletion::kImpossible:ExceptionalCompletion::kMayThrow,{i}});
    const auto prior = g.values[input.value].first_use;
    OperandUse use{id,returned?OperandRole::kReturnValue:OperandRole::kBase,ReadPurpose::kOperand,input,{},{},{},{}};
    use.next=prior; g.uses.push_back(use);
    if(prior) g.uses[prior->value].previous=UseId{i};g.values[input.value].first_use=UseId{i};
  }
  g.blocks={{{0},count+1,{0},0}}; return g;
}
int main() {
  abc::DecodedInstruction decoded;
  decoded.operand_count=2;decoded.operands[0]={0,abc::OperandKind::kImmediate};decoded.operands[1]={201,abc::OperandKind::kString};
  for(const auto opcode:{abc::Opcode::kLDSUPERBYNAME_IMM8_ID16,abc::Opcode::kLDSUPERBYNAME_IMM16_ID16}) {
    decoded.opcode=opcode;
    const auto read=std::get<NamedRead>(LiftNamedRead(decoded));Check(read.lookup==PropertyLookup::kSuper&&read.name.value==201);
    auto wrong=decoded;wrong.operands[1].kind=abc::OperandKind::kRegister;
    Check(std::get<NamedReadError>(LiftNamedRead(wrong))==NamedReadError::kMalformedInstruction);
  }
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
    auto evaluation=std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    auto control=std::get<ControlPlan>(BuildControlPlan(graph));
    FunctionSyntax syntax{};syntax.callable=CallableKind::kMethod;syntax.strictness=Strictness::kStrict;
    syntax.home_object=HomeObjectBinding::kClass;syntax.names={u"value"};
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for(std::uint32_t i=0;i<n;++i)constants[i]=NameId{0};
    const ExpressionConstants resolved{constants,1,0};
    auto built=BuildFunctionTree(graph,control,evaluation,syntax,resolved);
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree=std::get<ConstructedFunction>(built).tree;
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,evaluation,tree,resolved)));
    auto wrong=tree;wrong.home_object=HomeObjectBinding::kNone;
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph,control,evaluation,wrong,resolved)));
    wrong=tree;
    for(auto& node:wrong.expressions)if(auto* read=std::get_if<MemberExpr>(&node.expression)){read->lookup=PropertyLookup::kObject;break;}
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph,control,evaluation,wrong,resolved)));
    wrong=tree;
    for(auto& node:wrong.expressions)if(auto* read=std::get_if<ThisExpr>(&node.expression)){++read->owner.value;break;}
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph,control,evaluation,wrong,resolved)));
    SemanticBudget budget; budget.work=std::get<ConstructedFunction>(built).work;
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
    budget={};budget.bytes=0;
    Check(std::get<FunctionTreeFailure>(BuildFunctionTree(graph,control,evaluation,syntax,resolved,budget)).kind==FunctionTreeError::kResourceLimit);
  }
}
