#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "../../../src/source/family_print.h"
#include "../../support/analysis/ssa_fixture.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
namespace {
enum class Form : unsigned {
  kLocal, kCaptured, kEarlierCapture, kInitializerCapture, kMutableCapture,
  kSharedLoop, kIterationLoop, kEarlierRead, kInitializerRead, kNestedLocal,
  kFinallyRead, kAsync, kAsyncEarlier, kGenerator, kGeneratorEarlier, kNestedCapture
};
struct Fixture {
  FunctionTree tree{};
  StmtId declaration{}, store{};
  Fixture() {
    tree.callable=CallableKind::kFunction; tree.strictness=Strictness::kStrict;
    tree.parameter_scope={0}; tree.body_scope={1}; tree.body={0};
    tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
    tree.names={u"seed",u"report",u"again",u"saved"}; tree.origins={OriginalSite{{0},{0}}};
    for(unsigned i=0;i<3;++i) {
      tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});
      tree.parameters.push_back({{i},{},false});
    }
    tree.symbols.push_back({{1},{3},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kDebugRange,{}});
    tree.statements.push_back({BlockStmt{{1},0,0},{0}});
  }
  ExprId Expr(TreeExpression node) {ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({std::move(node),{0}});return id;}
  StmtId Stmt(TreeStatement node) {StmtId id{static_cast<std::uint32_t>(tree.statements.size())};tree.statements.push_back({std::move(node),{0}});return id;}
  ExprId Read(unsigned symbol) {return Expr(SymbolExpr{{symbol}});}
  ExprId Call(unsigned symbol,std::vector<ExprId> args={}) {
    const auto first=static_cast<std::uint32_t>(tree.arguments.size());tree.arguments.insert(tree.arguments.end(),args.begin(),args.end());
    return Expr(CallExpr{Read(symbol),{first,static_cast<std::uint32_t>(args.size())},CallReference::kValue});
  }
  StmtId Report(ExprId value) {return Stmt(ExpressionStmt{Call(1,{value})});}
  StmtId Block(const std::vector<StmtId>& children,ScopeId parent={1}) {
    const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,parent});
    const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),children.begin(),children.end());
    return Stmt(BlockStmt{scope,first,static_cast<std::uint32_t>(children.size())});
  }
  void Finish(const std::vector<StmtId>& children) {
    const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),children.begin(),children.end());
    std::get<BlockStmt>(tree.statements[0].statement)={{1},first,static_cast<std::uint32_t>(children.size())};
    for(unsigned i=0;i<tree.statements.size();++i)if(const auto* block=std::get_if<BlockStmt>(&tree.statements[i].statement))for(unsigned j=0;j<=block->child_count;++j) {
      const auto anchor=static_cast<std::uint32_t>(tree.anchors.size());tree.anchors.push_back({BlockBoundary{{i},j},{0}});tree.operations.push_back({{anchor},AnchorId{anchor}});
    }
  }
  void Reverse() {
    const auto expr=[&](ExprId id){return ExprId{static_cast<std::uint32_t>(tree.expressions.size()-1-id.value)};};
    const auto stmt=[&](StmtId id){return StmtId{static_cast<std::uint32_t>(tree.statements.size()-1-id.value)};};
    for(auto& node:tree.expressions)std::visit([&](auto& syntax){using T=std::decay_t<decltype(syntax)>;
      if constexpr(std::is_same_v<T,CallExpr>)syntax.callee=expr(syntax.callee);
      else if constexpr(std::is_same_v<T,AssignmentExpr>)syntax.value=expr(syntax.value);
      else if constexpr(std::is_same_v<T,AwaitExpr>||std::is_same_v<T,YieldExpr>||std::is_same_v<T,UnaryExpr>)syntax.operand=expr(syntax.operand);
      else test::Check(std::holds_alternative<SymbolExpr>(node.expression)||std::holds_alternative<TreeLiteral>(node.expression)||std::holds_alternative<FunctionExpr>(node.expression));
    },node.expression);
    for(auto& argument:tree.arguments)argument=expr(argument);
    for(auto& node:tree.statements)std::visit([&](auto& syntax){using T=std::decay_t<decltype(syntax)>;
      if constexpr(std::is_same_v<T,ExpressionStmt>)syntax.expression=expr(syntax.expression);
      else if constexpr(std::is_same_v<T,ReturnStmt>){if(syntax.value)syntax.value=expr(*syntax.value);}
      else if constexpr(std::is_same_v<T,DeclarationStmt>){if(syntax.initializer)syntax.initializer=expr(*syntax.initializer);}
      else if constexpr(std::is_same_v<T,IfStmt>){syntax.condition=expr(syntax.condition);syntax.consequent=stmt(syntax.consequent);if(syntax.alternate)syntax.alternate=stmt(*syntax.alternate);}
      else if constexpr(std::is_same_v<T,LoopStmt>)syntax.body=stmt(syntax.body);
      else if constexpr(std::is_same_v<T,BreakStmt>)syntax.target=stmt(syntax.target);
      else if constexpr(std::is_same_v<T,TryStmt>){syntax.body=stmt(syntax.body);if(syntax.finalizer)syntax.finalizer=stmt(*syntax.finalizer);}
      else test::Check(std::holds_alternative<BlockStmt>(node.statement));
    },node.statement);
    for(auto& child:tree.children)child=stmt(child);
    for(auto& anchor:tree.anchors)std::get<BlockBoundary>(anchor.position).block=stmt(std::get<BlockBoundary>(anchor.position).block);
    tree.body=stmt(tree.body);declaration=stmt(declaration);store=stmt(store);
    std::reverse(tree.expressions.begin(),tree.expressions.end());std::reverse(tree.statements.begin(),tree.statements.end());
  }
};
FunctionTree Child(bool mutable_capture) {
  FunctionTree tree{};tree.owner={1};tree.callable=CallableKind::kArrow;tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0};tree.body_scope={1};tree.capture_scope=ScopeId{2};tree.body={0};
  tree.scopes={{ScopeKind::kParameters,ScopeId{2}},{ScopeKind::kFunctionBody,ScopeId{0}},{ScopeKind::kCaptures,{}}};
  tree.names={u"saved",u"value",u"write"};tree.origins={OriginalSite{{0},{0}}};
  tree.symbols={{{2},{0},BindingKind::kCapture,DeclarationKind::kLet,NameEvidence::kDebugRange,{}}};
  tree.captures={{{0},{{0},{3}}}};
  tree.expressions={{SymbolExpr{{0}},{0}}};
  tree.statements={{BlockStmt{{1},0,1},{0}},{ReturnStmt{ExprId{0}},{0}}};tree.children={{1}};
  if(mutable_capture) {
    for(unsigned i=1;i<=2;++i){tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});tree.parameters.push_back({{i},{},false});}
    tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
    tree.expressions.push_back({SymbolExpr{{1}},{0}});tree.expressions.push_back({AssignmentExpr{SymbolTarget{{0}},ExprId{1}},{0}});tree.expressions.push_back({SymbolExpr{{2}},{0}});
    tree.statements.push_back({ExpressionStmt{ExprId{2}},{0}});tree.statements.push_back({BlockStmt{{3},0,1},{0}});tree.statements.push_back({IfStmt{ExprId{3},StmtId{3},{}},{0}});
    tree.children={{2},{4},{1}};std::get<BlockStmt>(tree.statements[0].statement)={{1},1,2};
  }
  return tree;
}
std::uint64_t Check(Form form,unsigned count,bool reverse,bool emit) {
  Fixture fixture;auto& tree=fixture.tree;
  const bool async=form==Form::kAsync||form==Form::kAsyncEarlier;
  const bool generator=form==Form::kGenerator||form==Form::kGeneratorEarlier;
  const bool loop=form==Form::kSharedLoop||form==Form::kIterationLoop;
  const bool before=form==Form::kEarlierCapture||form==Form::kAsyncEarlier||form==Form::kGeneratorEarlier;
  const bool captured=form!=Form::kLocal&&form!=Form::kEarlierRead&&form!=Form::kInitializerRead&&form!=Form::kNestedLocal&&form!=Form::kFinallyRead;
  if(async)tree.callable=CallableKind::kAsyncFunction;if(generator)tree.callable=CallableKind::kGenerator;
  fixture.declaration=fixture.Stmt(DeclarationStmt{{3},{}});
  std::optional<ExprId> closure;
  if(captured){tree.closure_captures={{3}};closure=fixture.Expr(FunctionExpr{{1},{0,1}});}
  std::vector<StmtId> body;
  if(before)body.push_back(fixture.Report(*closure));
  if(form==Form::kEarlierRead)body.push_back(fixture.Report(fixture.Read(3)));
  for(unsigned i=0;i<count;++i)body.push_back(fixture.Report(fixture.Expr(TreeLiteral{IntegerLiteral{0}})));
  std::vector<ExprId> args;
  if(form==Form::kInitializerCapture)args.push_back(*closure);
  if(form==Form::kInitializerRead)args.push_back(fixture.Read(3));
  auto value=fixture.Call(0,args);if(async)value=fixture.Expr(AwaitExpr{value});if(generator)value=fixture.Expr(YieldExpr{value});
  fixture.store=fixture.Stmt(ExpressionStmt{fixture.Expr(AssignmentExpr{SymbolTarget{{3}},value})});body.push_back(fixture.store);
  if(captured&&!before&&form!=Form::kInitializerCapture) {
    auto report=fixture.Report(*closure);if(form==Form::kNestedCapture)report=fixture.Block({report});body.push_back(report);
  } else body.push_back(fixture.Report(fixture.Read(3)));
  body.push_back(fixture.Stmt(ReturnStmt{loop?std::nullopt:std::optional{fixture.Read(3)}}));
  bool fold=!before&&form!=Form::kInitializerCapture&&form!=Form::kEarlierRead&&form!=Form::kInitializerRead&&form!=Form::kFinallyRead&&form!=Form::kSharedLoop;
  if(loop) {
    const auto returned=body.back();body.pop_back();
    const auto block=fixture.Block({});const auto scope=std::get<BlockStmt>(tree.statements[block.value].statement).scope;
    const auto repeat=fixture.Stmt(LoopStmt{block,{}});
    const auto exit=fixture.Block({fixture.Stmt(BreakStmt{repeat})},scope);
    const auto guard=fixture.Stmt(IfStmt{fixture.Expr(UnaryExpr{TreeUnary::kNot,fixture.Call(2)}),exit,{}});
    body.insert(body.begin(),guard);
    if(form==Form::kIterationLoop){tree.symbols[3].scope=scope;body.insert(body.begin()+1,fixture.declaration);}
    const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),body.begin(),body.end());
    std::get<BlockStmt>(tree.statements[block.value].statement)={scope,first,static_cast<std::uint32_t>(body.size())};
    body={repeat,returned};if(form==Form::kSharedLoop)body.insert(body.begin(),fixture.declaration);
  } else if(form==Form::kNestedLocal||form==Form::kFinallyRead) {
    auto block=fixture.Block(body);
    if(form==Form::kFinallyRead)block=fixture.Stmt(TryStmt{block,{},fixture.Block({fixture.Report(fixture.Read(3))})});
    body={fixture.declaration,block};
  } else body.insert(body.begin(),fixture.declaration);
  fixture.Finish(body);if(reverse)fixture.Reverse();
  const auto checked=VerifyTreeStructure(tree);test::Check(std::holds_alternative<TreeStructure>(checked));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  if(bool(plan.statements[fixture.store.value])!=fold)std::cerr<<"local form="<<static_cast<unsigned>(form)<<", count="<<count<<", reverse="<<reverse<<" expected initializer="<<fold<<'\n';
  test::Check(bool(plan.statements[fixture.store.value])==fold);
  test::Check(!plan.aliases[3]&&!plan.literals[3]);
  if(fold)test::Check(plan.statements[fixture.store.value]->declaration==DeclarationKind::kLet);
  std::ostringstream output;ConstructedFamily family;family.root={0};family.members={{tree,{},{}}};
  if(captured)family.members.push_back({Child(form==Form::kMutableCapture),{},{}});
  const auto printed=PrintFamilyDraft(family,output);test::Check(std::holds_alternative<PrintedFamilyDraft>(printed));
  const auto& ranges=std::get<PrintedFamilyDraft>(printed).members[0];
  for(const auto range:ranges.expressions)test::Check(range.begin<range.end&&range.end<=output.str().size());
  for(auto anchor:ranges.anchors)test::Check(anchor>0&&anchor<=output.str().size());
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"form",static_cast<unsigned>(form)},{"count",count},{"fold",fold},{"reverse",reverse}}<<'\n';
  return plan.work;
}
std::uint64_t CaptureEdges(unsigned count) {
  Fixture fixture;auto& tree=fixture.tree;auto child=Child(false);
  std::vector<StmtId> declarations;
  tree.symbols.resize(3);tree.names.resize(3);child.symbols.clear();child.names.clear();child.captures.clear();
  child.expressions.clear();child.children.clear();child.statements={{BlockStmt{{1},0,count},{0}}};
  for(unsigned i=0;i<count;++i) {
    const auto name="cell"+std::to_string(i);const std::u16string spelling(name.begin(),name.end());
    tree.names.push_back(spelling);tree.symbols.push_back({{1},{i+3},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kDebugRange,{}});
    declarations.push_back(fixture.Stmt(DeclarationStmt{{i+3},{}}));tree.closure_captures.push_back({i+3});
    child.names.push_back(spelling);child.symbols.push_back({{2},{i},BindingKind::kCapture,DeclarationKind::kLet,NameEvidence::kDebugRange,{}});
    child.captures.push_back({{i},{{0},{i+3}}});child.expressions.push_back({SymbolExpr{{i}},{0}});
    child.statements.push_back({i+1==count?TreeStatement{ReturnStmt{ExprId{i}}}:TreeStatement{ExpressionStmt{ExprId{i}}},{0}});child.children.push_back({i+1});
  }
  const auto body=fixture.Block({}),loop=fixture.Stmt(LoopStmt{body,{}});
  const auto scope=std::get<BlockStmt>(tree.statements[body.value].statement).scope;
  const auto exit=fixture.Block({fixture.Stmt(BreakStmt{loop})},scope);
  const auto condition=fixture.Expr(UnaryExpr{TreeUnary::kNot,fixture.Call(1,{fixture.Expr(FunctionExpr{{1},{0,count}})})});
  const auto guard=fixture.Stmt(IfStmt{condition,exit,{}});
  const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.push_back(guard);
  std::get<BlockStmt>(tree.statements[body.value].statement)={scope,first,1};declarations.push_back(loop);fixture.Finish(declarations);
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));test::Check(loops.conditions[loop.value].has_value());
  const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  const analysis::SemanticBudget exact{plan.allocated_bytes,plan.work};
  test::Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,exact)));
  std::ostringstream output;ConstructedFamily family;family.root={0};family.members={{tree,{},{}},{child,{},{}}};
  test::Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family,output)));
  return plan.work;
}
}
int main(int argc,char**) {
  for(unsigned form=0;form<=static_cast<unsigned>(Form::kNestedCapture);++form) {
    std::uint64_t small=0,large=0;
    for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
      const auto work=Check(static_cast<Form>(form),count,reverse,argc>1);
      if(count==400&&!reverse)small=work;if(count==800&&!reverse)large=work;
    }
    test::Check(large<small*3);
  }
  std::uint64_t small=0,large=0;
  for(unsigned count:{1U,200U,201U,400U,800U}) {
    const auto work=CaptureEdges(count);if(count==400)small=work;if(count==800)large=work;
  }
  test::Check(large<small*3);
}
