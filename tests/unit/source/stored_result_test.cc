#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
namespace {
enum class Form { kRead, kStore, kPrefix, kDecrement, kPostfix, kSequence,
  kChoice, kDifferentChoice, kAnd, kOr, kNullish, kCall, kMember, kMemberStore,
  kAwait, kYield, kBeforeDeclaration, kNestedStore, kStoreAwait, kStoreYield,
  kNestedRead, kSameReadChoice };
struct Fixture {
  FunctionTree tree{};
  Fixture(Form form, bool temporary) {
    tree.callable=(form==Form::kAwait||form==Form::kStoreAwait)?CallableKind::kAsyncFunction:(form==Form::kYield||form==Form::kStoreYield)?CallableKind::kGenerator:CallableKind::kFunction;
    tree.strictness=Strictness::kStrict;tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
    tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
    tree.names={u"input",u"report",u"other",u"flag",u"state",u"value"};tree.origins={OriginalSite{{0},{0}}};
    for(unsigned i=0;i<4;++i) {
      tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});
      tree.parameters.push_back({{i},{},false});
    }
    tree.symbols.push_back({{1},{4},temporary?BindingKind::kCompilerTemporary:BindingKind::kLocal,
      DeclarationKind::kLet,temporary?NameEvidence::kSynthesized:NameEvidence::kMetadata,
      temporary?std::optional{TemporaryReason::kMultipleUses}:std::nullopt});
    tree.statements.push_back({BlockStmt{{1},0,0},{0}});
  }
  ExprId Expr(TreeExpression value) {ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({std::move(value),{0}});return id;}
  StmtId Stmt(TreeStatement value) {StmtId id{static_cast<std::uint32_t>(tree.statements.size())};tree.statements.push_back({std::move(value),{0}});return id;}
  ExprId Read(unsigned symbol) {return Expr(SymbolExpr{{symbol}});}
  ExprId Store(ExprId rhs) {return Expr(AssignmentExpr{SymbolTarget{{4}},rhs});}
  ExprId Call(unsigned symbol, std::vector<ExprId> args) {
    const auto first=static_cast<std::uint32_t>(tree.arguments.size());tree.arguments.insert(tree.arguments.end(),args.begin(),args.end());
    return Expr(CallExpr{Read(symbol),{first,static_cast<std::uint32_t>(args.size())},CallReference::kValue});
  }
  ExprId Update(bool prefix=true,bool increment=true) {return Expr(UpdateExpr{{4},increment?UpdateDirection::kIncrement:UpdateDirection::kDecrement,prefix?UpdateResult::kPrefix:UpdateResult::kPostfix});}
  void Finish(std::vector<StmtId> body, bool reverse) {
    tree.children=body;std::get<BlockStmt>(tree.statements[0].statement).child_count=body.size();
    for(unsigned i=0;i<=body.size();++i){tree.anchors.push_back({BlockBoundary{{0},i},{0}});tree.operations.push_back({{i},AnchorId{i}});}
    if(!reverse)return;
    const auto remap=[&](ExprId id){return ExprId{static_cast<std::uint32_t>(tree.expressions.size()-1-id.value)};};
    for(auto& expression:tree.expressions)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,AssignmentExpr>){node.value=remap(node.value);if(auto* member=std::get_if<MemberTarget>(&node.target))member->base=remap(member->base);}
      else if constexpr(std::is_same_v<T,SequenceExpr>||std::is_same_v<T,LogicalExpr>){node.left=remap(node.left);node.right=remap(node.right);}
      else if constexpr(std::is_same_v<T,ConditionalExpr>){node.condition=remap(node.condition);node.consequent=remap(node.consequent);node.alternate=remap(node.alternate);}
      else if constexpr(std::is_same_v<T,CallExpr>)node.callee=remap(node.callee);
      else if constexpr(std::is_same_v<T,MemberExpr>)node.base=remap(node.base);
      else if constexpr(std::is_same_v<T,AwaitExpr>||std::is_same_v<T,YieldExpr>)node.operand=remap(node.operand);
      else test::Check(std::holds_alternative<SymbolExpr>(expression.expression)||std::holds_alternative<UpdateExpr>(expression.expression));
    },expression.expression);
    for(auto& node:tree.statements)std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,DeclarationStmt>){if(value.initializer)value.initializer=remap(*value.initializer);}
      else if constexpr(std::is_same_v<T,ExpressionStmt>)value.expression=remap(value.expression);
      else if constexpr(std::is_same_v<T,ReturnStmt>){if(value.value)value.value=remap(*value.value);}
    },node.statement);
    for(auto& arg:tree.arguments)arg=remap(arg);std::reverse(tree.expressions.begin(),tree.expressions.end());
  }
};
std::uint64_t Check(Form form,unsigned count,bool reverse,bool temporary,bool emit) {
  Fixture f(form,temporary);auto& tree=f.tree;
  const auto declaration=f.Stmt(DeclarationStmt{{4},f.Read(0)});
  ExprId rhs{};bool folded=true;
  switch(form) {
    case Form::kRead:case Form::kBeforeDeclaration:rhs=f.Read(4);break;
    case Form::kStore:rhs=f.Store(f.Call(2,{}));break;
    case Form::kPrefix:rhs=f.Update();break;
    case Form::kDecrement:rhs=f.Update(true,false);break;
    case Form::kPostfix:rhs=f.Update(false);folded=false;break;
    case Form::kSequence:rhs=f.Expr(SequenceExpr{f.Store(f.Read(0)),f.Update()});break;
    case Form::kChoice:rhs=f.Expr(ConditionalExpr{f.Read(3),f.Update(),f.Store(f.Call(2,{}))});break;
    case Form::kDifferentChoice:rhs=f.Expr(ConditionalExpr{f.Read(3),f.Update(),f.Call(2,{})});folded=false;break;
    case Form::kAnd:case Form::kOr:case Form::kNullish:rhs=f.Expr(LogicalExpr{form==Form::kAnd?TreeLogical::kAnd:form==Form::kOr?TreeLogical::kOr:TreeLogical::kNullish,f.Read(4),f.Store(f.Call(2,{}))});break;
    case Form::kCall:rhs=f.Expr(SequenceExpr{f.Update(),f.Call(2,{f.Read(4)})});folded=false;break;
    case Form::kMember:rhs=f.Expr(MemberExpr{f.Read(0),NameId{5}});folded=false;break;
    case Form::kMemberStore:rhs=f.Read(4);folded=false;break;
    case Form::kAwait:rhs=f.Expr(AwaitExpr{f.Store(f.Call(2,{}))});folded=false;break;
    case Form::kYield:rhs=f.Expr(YieldExpr{f.Store(f.Call(2,{}))});folded=false;break;
    case Form::kNestedStore:rhs=f.Store(f.Store(f.Read(0)));break;
    case Form::kStoreAwait:rhs=f.Store(f.Expr(AwaitExpr{f.Call(2,{})}));break;
    case Form::kStoreYield:rhs=f.Store(f.Expr(YieldExpr{f.Call(2,{})}));break;
    case Form::kNestedRead:rhs=f.Store(f.Read(4));break;
    case Form::kSameReadChoice:rhs=f.Expr(ConditionalExpr{f.Read(3),f.Read(4),f.Read(4)});break;
  }
  // Independent effects compose through comma without moving or duplicating.
  for(unsigned i=0;i<count;++i)rhs=f.Expr(SequenceExpr{f.Call(1,{f.Read(3)}),rhs});
  const auto assignment=form==Form::kMemberStore?f.Expr(AssignmentExpr{MemberTarget{f.Read(0),NameId{5}},rhs}):f.Store(rhs);
  const auto result=f.Stmt(ExpressionStmt{f.Call(1,{assignment})});
  std::vector<StmtId> body{declaration,result,f.Stmt(ReturnStmt{f.Read(4)})};
  if(form==Form::kBeforeDeclaration)std::swap(body[0],body[1]);
  f.Finish(body,reverse);
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  const auto id=reverse?tree.expressions.size()-1-assignment.value:assignment.value;
  if(plan.replacements[id].has_value()!=folded)std::cerr<<"form="<<static_cast<unsigned>(form)<<" count="<<count<<" reverse="<<reverse<<'\n';
  test::Check(plan.replacements[id].has_value()==folded);
  // Independent semantic expectation: reading an initialized local (including
  // an agreeing choice or nested redundant write) has no observable effect.
  // Coercion, calls, suspension, retained writes and TDZ reads still do.
  const bool inert=count==0 && (form==Form::kRead || form==Form::kNestedRead || form==Form::kSameReadChoice);
  test::Check((plan.evaluations[id]==ExpressionEvaluation::kInert)==inert);
  // Every final analysis describes the current projection. An assignment
  // removed by an earlier pass must not retain its old PutValue summary.
  for (std::size_t i=0;i<tree.expressions.size();++i)
    if (plan.replacements[i]) test::Check(!plan.normal_stores[i]);
  std::ostringstream out;const auto printed=PrintFunctionTree(tree,out);test::Check(std::holds_alternative<PrintedFunction>(printed));
  for(const auto range:std::get<PrintedFunction>(printed).expressions)test::Check(range.begin<range.end&&range.end<=out.str().size());
  if(emit)std::cout<<nlohmann::json{{"source",out.str()},{"form",form},{"count",count},{"folded",folded},{"temporary",temporary}}<<'\n';
  return plan.work;
}
}
int main(int argc,char**) {
  for(unsigned form=0;form<=static_cast<unsigned>(Form::kSameReadChoice);++form)for(bool temporary:{false,true}) {
    std::uint64_t small=0,large=0;
    for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
      const auto work=Check(static_cast<Form>(form),count,reverse,temporary,argc>1);
      if(count==400&&!reverse)small=work;if(count==800&&!reverse)large=work;
    }
    test::Check(large<small*3);
  }
}
