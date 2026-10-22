#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/super_store.h"
#include "abcd/analysis/named_write.h"
#include "abcd/analysis/value_write.h"
#include "../../support/analysis/ssa_fixture.h"
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
using test::Check;
ValueProgram Graph(std::uint32_t n, bool computed, bool split) {
  ValueProgram g;
  g.values.push_back({EntryDefinition{ReceiverEntry{}},ValueKinds::Language(),{}});
  g.values.push_back({EntryDefinition{ParameterEntry{0}},ValueKinds::Language(),{}});
  const auto use=[&](OpId owner,OperandRole role,ValueOperand value) {
    const UseId id{static_cast<std::uint32_t>(g.uses.size())};
    OperandUse u{owner,role,ReadPurpose::kOperand,value};
    if(const auto* v=std::get_if<ValueId>(&value)) {
      u.next=g.values[v->value].first_use;
      if(u.next)g.uses[u.next->value].previous=id;
      g.values[v->value].first_use=id;
    }
    g.uses.push_back(u);
  };
  for(std::uint32_t i=0;i<=n;++i) {
    const OpId id{i};const UseId first{static_cast<std::uint32_t>(g.uses.size())};
    if(i<n) {
      use(id,OperandRole::kBase,ValueId{0});
      if(computed)use(id,OperandRole::kKey,analysis::IntegerLiteral{1});
      use(id,OperandRole::kStoredValue,ValueId{1});
    } else use(id,OperandRole::kReturnValue,SpecialValue::kUndefined);
    const auto effects=i<n ? LanguageEffect::kHeapRead|LanguageEffect::kHeapWrite|LanguageEffect::kUserCode : LanguageEffect::kNone;
    g.origins.push_back({{i},{i},{}});
    g.ops.push_back({i==n ? ValueOperation{ValueReturn{}} : computed ? ValueOperation{ValuePropertyWrite{PropertyLookup::kSuper}} : ValueOperation{ValueNamedWrite{{0},PropertyLookup::kSuper}},
      {0},i,first,static_cast<std::uint32_t>(g.uses.size()-first.value),{},effects,effects,
      i<n?ExceptionalCompletion::kMayThrow:ExceptionalCompletion::kImpossible,{i}});
  }
  g.blocks={{{0},n+1,{0},0}};
  if(split) {
    g.blocks.clear();
    for(std::uint32_t i=0;i<=n;++i) {
      g.ops[i].block={i};g.ops[i].order=0;
      g.blocks.push_back({{i},1,{static_cast<std::uint32_t>(g.edges.size())},static_cast<std::uint8_t>(i<n?1:0)});
      if(i<n)g.edges.push_back({{i},{i+1},FlowEdgeKind::kFallthrough,0});
    }
  }
  return g;
}
void Source(std::uint32_t n,bool computed,bool split) {
  auto g=Graph(n,computed,split);
  FunctionSyntax syntax{};syntax.callable=CallableKind::kMethod;syntax.strictness=Strictness::kStrict;
  syntax.home_object=HomeObjectBinding::kClass;syntax.parameters.resize(1);syntax.names={u"value"};
  std::vector<ExpressionConstant> constants(g.ops.size());
  if(!computed)for(std::uint32_t i=0;i<n;++i)constants[i]=NameId{0};
  const ExpressionConstants resolved{constants,1,0};
  const auto evaluation=std::get<EvaluationPlan>(BuildEvaluationPlan(g));
  const auto control=std::get<ControlPlan>(BuildControlPlan(g));
  const auto run=[&](SemanticBudget b){return BuildFunctionTree(g,control,evaluation,syntax,resolved,b);};
  const auto result=run({});
  if(const auto* f=std::get_if<FunctionTreeFailure>(&result)) {std::cerr<<"super store tree failure "<<static_cast<int>(f->kind)<<'\n';std::abort();}
  const auto& good=std::get<ConstructedFunction>(result);
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(g,control,evaluation,good.tree,resolved)));
  auto wrong=good.tree;wrong.home_object=HomeObjectBinding::kNone;
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,control,evaluation,wrong,resolved)));
  wrong=good.tree;
  for(auto& node:wrong.expressions)if(auto* a=std::get_if<AssignmentExpr>(&node.expression)) {a->lookup=PropertyLookup::kObject;break;}
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,control,evaluation,wrong,resolved)));
  wrong=good.tree;
  for(auto& node:wrong.expressions)if(auto* self=std::get_if<ThisExpr>(&node.expression)){++self->owner.value;break;}
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,control,evaluation,wrong,resolved)));
  wrong=good.tree;
  for(auto& symbol:wrong.symbols)if(symbol.kind==BindingKind::kParameter){symbol.kind=BindingKind::kLocal;break;}
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,control,evaluation,wrong,resolved)));
  wrong=good.tree;
  for(auto& node:wrong.expressions)if(auto* a=std::get_if<AssignmentExpr>(&node.expression)) {
    a->value=std::get<MemberTarget>(a->target).base;break;
  }
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(g,control,evaluation,wrong,resolved)));
  SemanticBudget b;b.work=good.work;Check(std::holds_alternative<ConstructedFunction>(run(b)));--b.work;
  Check(std::get<FunctionTreeFailure>(run(b)).kind==FunctionTreeError::kWorkLimit);
  std::size_t lo=0,hi=SemanticBudget{}.bytes;
  while(lo<hi){const auto mid=lo+(hi-lo)/2;b={};b.bytes=mid;if(std::holds_alternative<ConstructedFunction>(run(b)))hi=mid;else lo=mid+1;}
  b={};b.bytes=lo;Check(std::holds_alternative<ConstructedFunction>(run(b)));--b.bytes;
  Check(std::get<FunctionTreeFailure>(run(b)).kind==FunctionTreeError::kResourceLimit);
}
int main() {
  static_assert(sizeof(AssignmentExpr)<=sizeof(CallExpr));
  for(auto n:{1U,200U,201U,400U,800U})for(bool computed:{false,true})for(bool split:{false,true})Source(n,computed,split);
  abc::DecodedInstruction d{};d.operand_count=3;d.operands[0]={0,abc::OperandKind::kImmediate};d.operands[1]={5,abc::OperandKind::kString};d.operands[2]={7,abc::OperandKind::kRegister};
  for(auto code:{abc::Opcode::kSTSUPERBYNAME_IMM8_ID16_V8,abc::Opcode::kSTSUPERBYNAME_IMM16_ID16_V8}){
    d.opcode=code;const auto w=std::get<NamedWrite>(LiftNamedWrite(d,8));Check(w.lookup==PropertyLookup::kSuper && w.object.value==7 && w.name.value==5);
    Check(std::get<NamedWriteError>(LiftNamedWrite(d,7))==NamedWriteError::kRegisterRange);
  }
  d.operands[1]={6,abc::OperandKind::kRegister};
  for(auto code:{abc::Opcode::kSTSUPERBYVALUE_IMM8_V8_V8,abc::Opcode::kSTSUPERBYVALUE_IMM16_V8_V8}){
    d.opcode=code;const auto w=std::get<ValueWrite>(LiftValueWrite(d,8));Check(w.lookup==PropertyLookup::kSuper && w.object.value==6);
    auto bad=d;bad.operands[2].kind=abc::OperandKind::kString;Check(std::get<ValueWriteError>(LiftValueWrite(bad,8))==ValueWriteError::kMalformedInstruction);
  }
}
