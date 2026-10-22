#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/analysis/value_operands.h"
#include "abcd/analysis/this_property.h"
#include "abcd/analysis/register_effect.h"
#include "analysis/ssa_fixture.h"
#include <iostream>
using namespace abcd;using namespace abcd::source;using namespace abcd::analysis;using test::Check;
ValueProgram Graph(std::uint32_t n) {
  ValueProgram g;
  for(std::uint32_t i=0;i<n;++i){g.values.push_back({OpResult{{i}},ValueKinds::Language(),{}});g.origins.push_back({{i},{i},{}});g.ops.push_back({ValueThisProperty{ThisPropertyKind::kReadName,abc::PoolIndex{0}},{0},i,{0},0,ValueId{i},LanguageEffect::kHeapRead|LanguageEffect::kUserCode,LanguageEffect::kHeapRead|LanguageEffect::kUserCode,ExceptionalCompletion::kMayThrow,{i}});}
  g.values.back().first_use=UseId{0};g.uses.push_back({OpId{n},OperandRole::kReturnValue,ReadPurpose::kOperand,ValueId{n-1},{},{},{},{}});
  g.origins.push_back({{n},{n},{}});g.ops.push_back({ValueReturn{},{0},n,{0},1,{},LanguageEffect::kNone,LanguageEffect::kNone,ExceptionalCompletion::kImpossible,{n}});g.blocks={{{0},n+1,{0},0}};return g;
}
int main(){
  using O=abc::Opcode;using K=ThisPropertyKind;
  for(const auto opcode:{O::kLDTHISBYNAME_IMM8_ID16,O::kLDTHISBYNAME_IMM16_ID16,O::kSTTHISBYNAME_IMM8_ID16,O::kSTTHISBYNAME_IMM16_ID16,O::kLDTHISBYVALUE_IMM8,O::kLDTHISBYVALUE_IMM16,O::kSTTHISBYVALUE_IMM8_V8,O::kSTTHISBYVALUE_IMM16_V8}){
    abc::DecodedInstruction d{};d.opcode=opcode;
    const bool named=opcode==O::kLDTHISBYNAME_IMM8_ID16||opcode==O::kLDTHISBYNAME_IMM16_ID16||opcode==O::kSTTHISBYNAME_IMM8_ID16||opcode==O::kSTTHISBYNAME_IMM16_ID16;
    const bool write=opcode==O::kSTTHISBYNAME_IMM8_ID16||opcode==O::kSTTHISBYNAME_IMM16_ID16||opcode==O::kSTTHISBYVALUE_IMM8_V8||opcode==O::kSTTHISBYVALUE_IMM16_V8;
    d.operand_count=named||write?2:1;d.operands[0].kind=abc::OperandKind::kImmediate;d.operands[1].kind=named?abc::OperandKind::kString:abc::OperandKind::kRegister;
    const auto raw=std::get<ThisProperty>(LiftThisProperty(d,1));Check(WritesThisProperty(raw.kind)==write&&NamedThisProperty(raw.kind)==named);
    const auto semantic=std::get<SemanticInstruction>(LiftSemanticInstruction({{0},d},abc::ProfileId::kDynamic1206,1));
    const auto operands=std::get<ValueOperands>(DescribeValueOperands(semantic));Check(bool(operands.write)==!write);Check(operands.count==(named?(write?1:0):(write?2:1)));
    Check(semantic.effects.exceptional==ExceptionalCompletion::kMayThrow);
    if(!named&&write){d.operands[1].raw_bits=1;Check(std::get<ThisPropertyError>(LiftThisProperty(d,1))==ThisPropertyError::kRegisterRange);d.operands[1].raw_bits=0;}
    d.operands[0].raw_bits=UINT64_MAX;Check(std::get<ThisPropertyError>(LiftThisProperty(d,1))==ThisPropertyError::kMalformedInstruction);
    d.operands[0].raw_bits=0;d.operand_count=0;Check(std::get<ThisPropertyError>(LiftThisProperty(d,1))==ThisPropertyError::kMalformedInstruction);
  }
  for(auto n:{1U,200U,201U,400U,800U}) for(const bool split:{false,true}) {
    auto g=Graph(n);
    if(split){g.blocks.clear();for(std::uint32_t i=0;i<g.ops.size();++i){g.ops[i].block={i};g.ops[i].order=0;const bool last=i+1==g.ops.size();g.blocks.push_back({{i},1,{static_cast<std::uint32_t>(g.edges.size())},static_cast<std::uint8_t>(last?0:1)});if(!last)g.edges.push_back({{i},{i+1},FlowEdgeKind::kFallthrough,0});}}
const auto evaluation=std::get<EvaluationPlan>(BuildEvaluationPlan(g));const auto control=std::get<ControlPlan>(BuildControlPlan(g));
    FunctionSyntax syntax{};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;syntax.names={u"p",u"q"};
    std::vector<ExpressionConstant> constants(n+1);for(std::size_t i=0;i<n;++i)constants[i]=NameId{0};const ExpressionConstants resolved{constants,2,0};
    auto run=[&](SemanticBudget b){return BuildFunctionTree(g,control,evaluation,syntax,resolved,b);};
    auto good=std::get<ConstructedFunction>(run({}));Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(g,control,evaluation,good.tree,resolved)));
    auto wrong=good.tree;for(auto& e:wrong.expressions)if(auto* p=std::get_if<ThisExpr>(&e.expression)){p->owner.value++;break;}
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,control,evaluation,wrong,resolved)));
    wrong=good.tree;wrong.callable=CallableKind::kArrow;Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,control,evaluation,wrong,resolved)));
    wrong=good.tree;for(auto& e:wrong.expressions)if(auto* p=std::get_if<MemberExpr>(&e.expression)){p->key=NameId{1};break;}Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,control,evaluation,wrong,resolved)));
    SemanticBudget b;b.work=good.work;Check(std::holds_alternative<ConstructedFunction>(run(b)));--b.work;Check(std::get<FunctionTreeFailure>(run(b)).kind==FunctionTreeError::kWorkLimit);
    std::size_t lo=0,hi=SemanticBudget{}.bytes;while(lo<hi){const auto m=lo+(hi-lo)/2;b={};b.bytes=m;if(std::holds_alternative<ConstructedFunction>(run(b)))hi=m;else lo=m+1;}b={};b.bytes=lo;Check(std::holds_alternative<ConstructedFunction>(run(b)));--b.bytes;Check(std::get<FunctionTreeFailure>(run(b)).kind==FunctionTreeError::kResourceLimit);
    std::cout<<n<<" "<<lo<<" bytes "<<good.work<<" work\n";
  }
}
