#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <array>
#include <bit>
#include <limits>
#include <sstream>
#include <nlohmann/json.hpp>
using namespace abcd;
using namespace abcd::source;
namespace {
struct Fixture {
  FunctionTree tree{}; StmtId declaration, store;
  ExprId Expr(TreeExpression node) {
    ExprId id{static_cast<std::uint32_t>(tree.expressions.size())}; tree.expressions.push_back({std::move(node), {0}}); return id;
  }
  StmtId Stmt(TreeStatement node) {
    StmtId id{static_cast<std::uint32_t>(tree.statements.size())}; tree.statements.push_back({std::move(node), {0}}); return id;
  }
  ExprId Read(unsigned symbol) { return Expr(SymbolExpr{{symbol}}); }
  ExprId Call(unsigned symbol, std::optional<unsigned> argument = {}) {
    auto first = static_cast<std::uint32_t>(tree.arguments.size()); if (argument) tree.arguments.push_back(Read(*argument));
    return Expr(CallExpr{Read(symbol), {first, argument ? 1U : 0U}, CallReference::kValue});
  }
  StmtId Store(ExprId value) { return Stmt(ExpressionStmt{Expr(AssignmentExpr{SymbolTarget{{2}}, value})}); }
  void Reverse() {
    const auto expression = [&](ExprId id) { return ExprId{static_cast<std::uint32_t>(tree.expressions.size()-1-id.value)}; };
    for (auto& node : tree.expressions) std::visit([&](auto& syntax) {
      using T = std::decay_t<decltype(syntax)>;
      if constexpr (std::is_same_v<T, AssignmentExpr>) syntax.value = expression(syntax.value);
      else if constexpr (std::is_same_v<T, CallExpr>) syntax.callee = expression(syntax.callee);
      else if constexpr (std::is_same_v<T, AwaitExpr> || std::is_same_v<T, YieldExpr>) syntax.operand = expression(syntax.operand);
      else if constexpr (std::is_same_v<T, UnaryExpr>) syntax.operand = expression(syntax.operand);
      else if constexpr (std::is_same_v<T, MemberExpr>) syntax.base = expression(syntax.base);
      else if constexpr (std::is_same_v<T, BinaryExpr>) { syntax.left = expression(syntax.left); syntax.right = expression(syntax.right); }
      else test::Check(std::holds_alternative<SymbolExpr>(node.expression) || std::holds_alternative<TreeLiteral>(node.expression) ||
          std::holds_alternative<EmptyObjectExpr>(node.expression) || std::holds_alternative<EmptyArrayExpr>(node.expression));
    }, node.expression);
    for (auto& argument : tree.arguments) argument = expression(argument);
    for (auto& node : tree.statements) std::visit([&](auto& syntax) {
      using T = std::decay_t<decltype(syntax)>;
      if constexpr (std::is_same_v<T, ExpressionStmt>) syntax.expression = expression(syntax.expression);
      else if constexpr (std::is_same_v<T, ReturnStmt>) { if (syntax.value) syntax.value = expression(*syntax.value); }
      else if constexpr (std::is_same_v<T, IfStmt>) syntax.condition = expression(syntax.condition);
      else if constexpr (std::is_same_v<T, DeclarationStmt>) { if (syntax.initializer) syntax.initializer = expression(*syntax.initializer); }
    }, node.statement);
    std::reverse(tree.expressions.begin(), tree.expressions.end());
    const auto statement = [&](StmtId id) { return StmtId{static_cast<std::uint32_t>(tree.statements.size()-1-id.value)}; };
    for (auto& child : tree.children) child = statement(child);
    for (auto& node : tree.statements) {
      if (auto* syntax = std::get_if<IfStmt>(&node.statement)) {
        syntax->consequent = statement(syntax->consequent);
        if (syntax->alternate) syntax->alternate = statement(*syntax->alternate);
      }
      if (auto* syntax = std::get_if<TryStmt>(&node.statement)) {
        syntax->body = statement(syntax->body);
        if (syntax->finalizer) syntax->finalizer = statement(*syntax->finalizer);
      }
      if (auto* syntax = std::get_if<LoopStmt>(&node.statement)) syntax->body = statement(syntax->body);
      if (auto* syntax = std::get_if<BreakStmt>(&node.statement)) syntax->target = statement(syntax->target);
    }
    for (auto& anchor : tree.anchors) std::get<BlockBoundary>(anchor.position).block = statement(std::get<BlockBoundary>(anchor.position).block);
    tree.body = statement(tree.body); declaration = statement(declaration); store = statement(store);
    std::reverse(tree.statements.begin(), tree.statements.end());
  }
  Fixture(unsigned form, unsigned count) {
    tree.callable = form == 5 ? CallableKind::kAsyncFunction : form == 6 ? CallableKind::kGenerator : CallableKind::kFunction;
    tree.strictness = Strictness::kStrict; tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.names = {u"seed", u"report", u"saved"}; tree.origins = {OriginalSite{{0}, {0}}};
    for (unsigned i = 0; i < 2; ++i) {
      tree.symbols.push_back({{0}, {i}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
      tree.parameters.push_back({{i}, {}, false});
    }
    tree.symbols.push_back({{1}, {2}, form == 7 ? BindingKind::kLocal : BindingKind::kCompilerTemporary,
        DeclarationKind::kLet, NameEvidence::kSynthesized, form == 7 ? std::optional<TemporaryReason>{} : TemporaryReason::kMultipleUses});
    tree.statements.push_back({BlockStmt{{1}, 0, 0}, {0}});
    declaration = Stmt(DeclarationStmt{{2}, {}});
    std::vector<StmtId> body{declaration};
    if (form == 2) body.push_back(Stmt(ExpressionStmt{Call(1, 2)}));
    ExprId value = form == 3 ? Read(2) : Call(0);
    if (form == 5) value = Expr(AwaitExpr{value}); if (form == 6) value = Expr(YieldExpr{value});
    store = Store(value);
    if (form == 4) {
      tree.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
      const auto first = static_cast<std::uint32_t>(tree.children.size()); tree.children.push_back(store);
      const auto block = Stmt(BlockStmt{{2}, first, 1});
      body.push_back(Stmt(IfStmt{Read(0), block, {}}));
    } else body.push_back(store);
    for (unsigned i = 0; i < count; ++i) body.push_back(Stmt(ExpressionStmt{Call(1, 2)}));
    if (form == 1) body.push_back(Store(Call(0)));
    body.push_back(Stmt(ReturnStmt{Read(2)}));
    if (form >= 11 && form <= 14) {
      if (form != 14) {
        std::get<ReturnStmt>(tree.statements[body.back().value].statement).value.reset();
        tree.expressions.pop_back();
      }
      tree.names.push_back(u"again");
      tree.symbols.push_back({{0}, {3}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
      tree.parameters.push_back({{3}, {}, false});
      std::optional<StmtId> carried;
      if (form == 12) {
        tree.names.push_back(u"carried");
        tree.symbols.push_back({{1}, {4}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
        carried = Stmt(DeclarationStmt{{4}, Expr(TreeLiteral{BooleanLiteral{true}})});
      }
      tree.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
      tree.scopes.push_back({ScopeKind::kBlock, ScopeId{2}});
      const auto loop = Stmt(LoopStmt{});
      const auto exit_first = static_cast<std::uint32_t>(tree.children.size());
      tree.children.push_back(Stmt(BreakStmt{loop}));
      const auto exit = Stmt(BlockStmt{{3}, exit_first, 1});
      const auto guard = Stmt(IfStmt{Expr(UnaryExpr{TreeUnary::kNot, form == 12 ? Read(4) : Call(3)}), exit, {}});
      std::optional<StmtId> second_store;
      if (form == 12) {
        tree.scopes.push_back({ScopeKind::kBlock, ScopeId{2}});
        const auto first = static_cast<std::uint32_t>(tree.children.size()); tree.children.push_back(Store(Call(0)));
        second_store = Stmt(IfStmt{Read(1), Stmt(BlockStmt{{4}, first, 1}), {}});
      }
      const auto loop_first = static_cast<std::uint32_t>(tree.children.size()); tree.children.push_back(guard);
      if (form >= 13) {
        tree.scopes.push_back({ScopeKind::kBlock, ScopeId{2}});
        tree.names.push_back(u"fence");
        tree.symbols.push_back({{4}, {4}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
        const auto first = static_cast<std::uint32_t>(tree.children.size());
        tree.children.push_back(Stmt(DeclarationStmt{{4}, {}}));
        tree.children.insert(tree.children.end(), body.begin() + 1, body.end() - 1);
        const auto arm = Stmt(BlockStmt{{4}, first, static_cast<std::uint32_t>(body.size() - 1)});
        std::get<IfStmt>(tree.statements[guard.value].statement).alternate = arm;
      } else tree.children.insert(tree.children.end(), body.begin() + 1, body.end() - 1);
      if (second_store) tree.children.insert(tree.children.begin() + loop_first + 2, *second_store);
      if (form == 12) tree.children.push_back(Stmt(ExpressionStmt{Expr(AssignmentExpr{SymbolTarget{{4}}, Call(3, 2)})}));
      const auto nested = Stmt(BlockStmt{{2}, loop_first,
          form >= 13 ? 1U : static_cast<std::uint32_t>(body.size() - 1 + (form == 12 ? 2 : 0))});
      std::get<LoopStmt>(tree.statements[loop.value].statement).body = nested;
      body = {declaration, loop, body.back()};
      if (carried) body.insert(body.begin() + 1, *carried);
      // No observation after the loop: each iteration's private lifetime can
      // shrink, while source cells/captures remain outside this projection.
    } else if (form >= 8) {
      // Scope confinement is independent of the outer declaration location.
      // Finally's old-value read must prevent sinking into the protected body.
      tree.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
      const auto nested_first = static_cast<std::uint32_t>(tree.children.size());
      tree.children.insert(tree.children.end(), body.begin() + 1, body.end());
      const auto nested = Stmt(BlockStmt{{2}, nested_first, static_cast<std::uint32_t>(body.size() - 1)});
      body = {declaration};
      if (form == 8) {
        body.push_back(Stmt(IfStmt{Read(0), nested, {}}));
        body.push_back(Stmt(ReturnStmt{}));
      } else {
        tree.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
        const auto final_first = static_cast<std::uint32_t>(tree.children.size());
        tree.children.push_back(Stmt(ExpressionStmt{Call(1, form == 10 ? std::optional<unsigned>{2} : std::nullopt)}));
        const auto finalizer = Stmt(BlockStmt{{3}, final_first, 1});
        body.push_back(Stmt(TryStmt{nested, {}, finalizer}));
      }
    }
    const auto first = static_cast<std::uint32_t>(tree.children.size()); tree.children.insert(tree.children.end(), body.begin(), body.end());
    std::get<BlockStmt>(tree.statements[0].statement) = {{1}, first, static_cast<std::uint32_t>(body.size()), {}};
    for (unsigned i = 0; i <= body.size(); ++i) {
      tree.anchors.push_back({BlockBoundary{{0}, i}, {0}});
      tree.operations.push_back({{i}, AnchorId{i}});
    }
  }
};
std::uint64_t Constant(unsigned form, unsigned count, unsigned literal, bool reverse, bool emit) {
  Fixture fixture(3, count); auto& tree = fixture.tree;
  const auto rhs = std::get<AssignmentExpr>(tree.expressions[std::get<ExpressionStmt>(tree.statements[fixture.store.value].statement).expression.value].expression).value;
  const auto bits = [](double value) { return NumberLiteral{std::bit_cast<std::uint64_t>(value)}; };
  tree.texts.push_back(u"\u0100\n\U0001d4b3");
  const std::array<TreeLiteral, 10> literals{UndefinedLiteral{}, NullLiteral{}, BooleanLiteral{false}, BooleanLiteral{true},
      IntegerLiteral{42}, bits(-0.0), bits(std::numeric_limits<double>::quiet_NaN()),
      bits(std::numeric_limits<double>::infinity()), bits(-std::numeric_limits<double>::infinity()), TextId{0}};
  if (form != 6) tree.expressions[rhs.value].expression = form == 13 ? TreeLiteral{IntegerLiteral{-2}} : literals[literal];
  if (form == 14) tree.expressions[rhs.value].expression = EmptyObjectExpr{};
  if (form == 15) tree.expressions[rhs.value].expression = EmptyArrayExpr{};
  auto roots = tree.children; tree.children.clear();
  const auto block = [&](const std::vector<StmtId>& children, ScopeId parent) {
    const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,parent});
    const auto first = static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),children.begin(),children.end());
    return fixture.Stmt(BlockStmt{scope,first,static_cast<std::uint32_t>(children.size())});
  };
  if (form == 1) {
    unsigned previous = 2; const auto last = count ? static_cast<unsigned>(tree.symbols.size()) + count - 1 : previous;
    for (auto& expression : tree.expressions) if (auto* read = std::get_if<SymbolExpr>(&expression.expression);read && read->symbol == SymbolId{2}) read->symbol = {last};
    std::vector<StmtId> declarations, stores;
    for (unsigned i = 0; i < count; ++i) {
      const auto symbol = static_cast<std::uint32_t>(tree.symbols.size()), name = static_cast<std::uint32_t>(tree.names.size());
      const auto digits = std::to_string(i);tree.names.push_back(u"copy"+std::u16string(digits.begin(),digits.end()));
      tree.symbols.push_back({{1},{name},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
      declarations.push_back(fixture.Stmt(DeclarationStmt{{symbol},{}}));
      stores.push_back(fixture.Stmt(ExpressionStmt{fixture.Expr(AssignmentExpr{SymbolTarget{{symbol}},fixture.Read(previous)})}));previous = symbol;
    }
    roots.insert(roots.begin()+2,stores.begin(),stores.end());roots.insert(roots.begin(),declarations.begin(),declarations.end());
  } else if (form == 2) roots.insert(roots.begin()+1,fixture.Stmt(ExpressionStmt{fixture.Call(1,2)}));
  else if (form == 3) roots.insert(roots.end()-1,fixture.Store(fixture.Call(0)));
  else if (form == 4) roots[1] = fixture.Stmt(IfStmt{fixture.Read(0),block({fixture.store},{1}),{}});
  else if (form == 5) {tree.symbols[2].kind = BindingKind::kLocal;tree.symbols[2].temporary.reset();}
  else if (form == 7) {
    std::vector<StmtId> body(roots.begin()+1,roots.end()-1);body.insert(body.begin(),fixture.Stmt(ExpressionStmt{fixture.Call(0)}));
    const auto protected_body = block(body,{1}), finalizer = block({fixture.Stmt(ExpressionStmt{fixture.Call(1,2)})},{1});
    roots = {roots.front(),fixture.Stmt(TryStmt{protected_body,{},finalizer}),roots.back()};
  } else if (form == 8 || form == 9) {
    const auto loop = fixture.Stmt(LoopStmt{{0},{}});
    const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
    const auto stops = block({fixture.Stmt(BreakStmt{loop,{}})},scope);
    std::vector<StmtId> body{fixture.Stmt(IfStmt{fixture.Expr(UnaryExpr{TreeUnary::kNot,fixture.Call(0)}),stops,{}})};
    body.insert(body.end(),roots.begin()+1,roots.end()-1);
    const auto first = static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),body.begin(),body.end());
    const auto owned = fixture.Stmt(BlockStmt{scope,first,static_cast<std::uint32_t>(body.size())});
    std::get<LoopStmt>(tree.statements[loop.value].statement).body = owned;
    if (form == 9) tree.expressions[std::get<ReturnStmt>(tree.statements[roots.back().value].statement).value->value].expression = TreeLiteral{UndefinedLiteral{}};
    roots = {roots.front(),loop,roots.back()};
  } else if (form == 10 || form == 11) {
    tree.callable = form == 10 ? CallableKind::kAsyncFunction : CallableKind::kGenerator;
    const auto call = fixture.Call(0);const auto suspended = form == 10 ? fixture.Expr(AwaitExpr{call}) : fixture.Expr(YieldExpr{call});
    roots.insert(roots.begin()+2,fixture.Stmt(ExpressionStmt{suspended}));
  } else if (form == 12 || form == 13) {
    const auto returned = *std::get<ReturnStmt>(tree.statements[roots.back().value].statement).value;
    const auto base = fixture.Read(2);
    if (form == 12) {
      const NameId name{static_cast<std::uint32_t>(tree.names.size())};tree.names.push_back(u"toString");
      const auto member = fixture.Expr(MemberExpr{base,name});
      tree.expressions[returned.value].expression = CallExpr{member,{0,0},CallReference::kMember};
    } else tree.expressions[returned.value].expression = BinaryExpr{analysis::BinaryOperator::kExponent,base,fixture.Expr(TreeLiteral{IntegerLiteral{2}})};
  }
  const auto first = static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),roots.begin(),roots.end());
  std::get<BlockStmt>(tree.statements[tree.body.value].statement) = {{1},first,static_cast<std::uint32_t>(roots.size())};
  tree.anchors.clear();tree.operations.clear();
  for (unsigned i = 0;i<tree.statements.size();++i) if (const auto* body = std::get_if<BlockStmt>(&tree.statements[i].statement))
    for (unsigned child = 0;child<=body->child_count;++child) {
      const auto anchor = static_cast<std::uint32_t>(tree.anchors.size());tree.anchors.push_back({BlockBoundary{{i},child},{0}});tree.operations.push_back({{anchor},AnchorId{anchor}});
    }
  if (reverse) fixture.Reverse();test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops = std::get<LoopSyntax>(PlanLoopSyntax(tree));
  const auto nullish = std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto result = PlanBindingInitializers(tree,loops,nullish);test::Check(std::holds_alternative<BindingInitializers>(result));
  const auto& plan = std::get<BindingInitializers>(result);
  const bool folded = form < 2 || (form >= 9 && form < 14);
  std::ostringstream output;const auto printed = PrintFunctionTree(tree,output);test::Check(std::holds_alternative<PrintedFunction>(printed));
  test::Check((output.str().find("saved") == std::string::npos) == folded);
  for (const auto range : std::get<PrintedFunction>(printed).expressions) test::Check(range.begin<=range.end && range.end<=output.str().size());
  for (const auto anchor : std::get<PrintedFunction>(printed).anchors) test::Check(anchor>0 && anchor<=output.str().size());
  if (emit) std::cout<<nlohmann::json{{"source",output.str()},{"constant",form},{"count",count},{"literal",literal},{"folded",folded}}<<'\n';
  return plan.work;
}
std::uint64_t Alias(unsigned form,unsigned count,bool reverse,bool emit) {
  Fixture fixture(form==9||form==10?3:0,form==15?0:count);auto& tree=fixture.tree;
  const auto original=tree.children;tree.children.clear();
  const auto last=count?static_cast<std::uint32_t>(tree.symbols.size()+count-1):2;
  for(auto& expression:tree.expressions)if(auto* read=std::get_if<SymbolExpr>(&expression.expression);read&&read->symbol==SymbolId{2})read->symbol={last};
  std::vector<StmtId> declarations,stores;
  unsigned previous=2;
  for(unsigned i=0;i<count;++i) {
    const auto symbol=static_cast<std::uint32_t>(tree.symbols.size()),name=static_cast<std::uint32_t>(tree.names.size());
    const auto digits=std::to_string(i);tree.names.push_back(u"copy"+std::u16string(digits.begin(),digits.end()));
    tree.symbols.push_back({{1},{name},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    declarations.push_back(fixture.Stmt(DeclarationStmt{{symbol},{}}));
    stores.push_back(fixture.Stmt(ExpressionStmt{fixture.Expr(AssignmentExpr{SymbolTarget{{symbol}},fixture.Read(previous)})}));previous=symbol;
  }
  std::vector<StmtId> body=declarations;body.push_back(fixture.declaration);body.push_back(fixture.store);
  body.insert(body.end(),stores.begin(),stores.end());body.insert(body.end(),original.begin()+2,original.end());
  const auto block=[&](const std::vector<StmtId>& children) {
    const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
    const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),children.begin(),children.end());
    return fixture.Stmt(BlockStmt{scope,first,static_cast<std::uint32_t>(children.size())});
  };
  const auto shadow=[&](StmtId owned) {
    const auto scope=std::get<BlockStmt>(tree.statements[owned.value].statement).scope;
    const auto symbol=static_cast<std::uint32_t>(tree.symbols.size());tree.symbols.push_back({scope,{2},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    const auto declared=fixture.Stmt(DeclarationStmt{{symbol},fixture.Expr(TreeLiteral{IntegerLiteral{123}})});
    auto& sequence=std::get<BlockStmt>(tree.statements[owned.value].statement);
    tree.children.insert(tree.children.begin()+sequence.first_child,declared);++sequence.child_count;
  };
  if(form==1)body.insert(body.end()-1,fixture.Store(fixture.Call(0)));
  if(form==2&&count)body.insert(body.begin()+declarations.size()+1,fixture.Stmt(ExpressionStmt{fixture.Call(1,last)}));
  if(form==3&&count) {
    const auto at=body.begin()+declarations.size()+2;
    const auto symbol=static_cast<std::uint32_t>(tree.symbols.size()),name=static_cast<std::uint32_t>(tree.names.size());tree.names.push_back(u"flag");
    tree.symbols.push_back({{0},{name},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kSynthesized,{}});tree.parameters.push_back({{symbol},{},false});
    const auto branch=fixture.Stmt(IfStmt{fixture.Read(symbol),block(stores),{}});
    body.erase(at,at+stores.size());body.insert(body.begin()+declarations.size()+2,branch);
  }
  if(form==4||form==5) {
    const auto first=declarations.size()+2+stores.size();std::vector<StmtId> nested(body.begin()+first,body.end());body.erase(body.begin()+first,body.end());
    const auto owned=block(nested);
    if(form==5)shadow(owned);
    body.push_back(owned);
  }
  if(form==14||form==15||form==16) {
    const auto first=declarations.size()+2+(count?1:0);
    const auto returned=body.back();if(form==15)tree.expressions[std::get<ReturnStmt>(tree.statements[returned.value].statement).value->value].expression=TreeLiteral{UndefinedLiteral{}};
    std::vector<StmtId> nested(body.begin()+first,form==15?body.end()-1:body.end());body.erase(body.begin()+first,body.end());
    if(form==16&&count)nested.insert(nested.end()-1,fixture.Stmt(ExpressionStmt{fixture.Expr(AssignmentExpr{SymbolTarget{{last}},fixture.Call(0)})}));
    const auto owned=block(nested);shadow(owned);body.push_back(owned);if(form==15)body.push_back(returned);
  }
  if(form==6) {
    const auto owned=block({});const auto scope=std::get<BlockStmt>(tree.statements[owned.value].statement).scope;
    const auto symbol=static_cast<std::uint32_t>(tree.symbols.size());tree.symbols.push_back({scope,{2},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.push_back(fixture.Stmt(DeclarationStmt{{symbol},fixture.Expr(TreeLiteral{IntegerLiteral{123}})}));
    std::get<BlockStmt>(tree.statements[owned.value].statement)={scope,first,1};body.insert(body.end()-1,owned);
  }
  if(form==7||form==8) {
    tree.callable=form==7?CallableKind::kAsyncFunction:CallableKind::kGenerator;
    const auto call=fixture.Call(0);const auto suspended=form==7?fixture.Expr(AwaitExpr{call}):fixture.Expr(YieldExpr{call});
    body.insert(body.begin()+declarations.size()+2+stores.size(),fixture.Stmt(ExpressionStmt{suspended}));
  }
  if(form==9||form==10) {
    const auto assignment=std::get<ExpressionStmt>(tree.statements[fixture.store.value].statement).expression;
    const auto rhs=std::get<AssignmentExpr>(tree.expressions[assignment.value].expression).value;
    tree.expressions[rhs.value].expression=form==9?TreeExpression{EmptyObjectExpr{}}:TreeExpression{EmptyArrayExpr{}};
  }
  if(form==11&&count)body.insert(body.end()-1,fixture.Stmt(ExpressionStmt{fixture.Expr(AssignmentExpr{SymbolTarget{{last}},fixture.Call(0)})}));
  if(form==12){tree.symbols[2].kind=BindingKind::kLocal;tree.symbols[2].temporary.reset();}
  if(form==13) {
    const auto returned=*std::get<ReturnStmt>(tree.statements[body.back().value].statement).value;
    tree.expressions[returned.value].expression=CallExpr{fixture.Read(last),{0,0},CallReference::kValue};
  }
  if(form==17) {
    const auto returned=body.back();const auto begin=declarations.size()+1;
    const auto protected_body=block(std::vector<StmtId>(body.begin()+begin,body.end()-1));
    const auto finalizer=block({fixture.Stmt(ExpressionStmt{fixture.Call(1,last)})});
    body.resize(begin);body.push_back(fixture.Stmt(TryStmt{protected_body,{},finalizer}));body.push_back(returned);
  }
  if(form==18||form==19) {
    const auto symbol=static_cast<std::uint32_t>(tree.symbols.size()),name=static_cast<std::uint32_t>(tree.names.size());tree.names.push_back(u"again");
    tree.symbols.push_back({{0},{name},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kSynthesized,{}});tree.parameters.push_back({{symbol},{},false});
    const auto returned=body.back();if(form==18)tree.expressions[std::get<ReturnStmt>(tree.statements[returned.value].statement).value->value].expression=TreeLiteral{UndefinedLiteral{}};
    const auto loop=fixture.Stmt(LoopStmt{{0},{}}),stops=block({fixture.Stmt(BreakStmt{loop,{}})});
    const auto begin=declarations.size()+1;std::vector<StmtId> nested{fixture.Stmt(IfStmt{fixture.Expr(UnaryExpr{TreeUnary::kNot,fixture.Call(symbol)}),stops,{}})};
    nested.insert(nested.end(),body.begin()+begin,body.end()-1);const auto owned=block(nested);
    tree.scopes[std::get<BlockStmt>(tree.statements[stops.value].statement).scope.value].parent=std::get<BlockStmt>(tree.statements[owned.value].statement).scope;
    std::get<LoopStmt>(tree.statements[loop.value].statement).body=owned;
    body.resize(begin);body.push_back(loop);body.push_back(returned);
  }
  if(form==20) {
    const auto first=declarations.size()+2+stores.size();std::vector<StmtId> nested(body.begin()+first,body.end());body.resize(first);
    auto owned=block(nested);
    for(unsigned i=0;i<count;++i) {
      const auto outer=block({owned});tree.scopes[std::get<BlockStmt>(tree.statements[owned.value].statement).scope.value].parent=std::get<BlockStmt>(tree.statements[outer.value].statement).scope;owned=outer;
    }
    body.push_back(owned);
  }
  const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),body.begin(),body.end());
  std::get<BlockStmt>(tree.statements[tree.body.value].statement)={{1},first,static_cast<std::uint32_t>(body.size())};
  tree.anchors.clear();tree.operations.clear();for(unsigned i=0;i<tree.statements.size();++i)if(const auto* sequence=std::get_if<BlockStmt>(&tree.statements[i].statement))for(unsigned at=0;at<=sequence->child_count;++at){const auto anchor=static_cast<std::uint32_t>(tree.anchors.size());tree.anchors.push_back({BlockBoundary{{i},at},{0}});tree.operations.push_back({{anchor},AnchorId{anchor}});}
  if(reverse) {
    fixture.Reverse();
    const auto symbol=[&](SymbolId id){return SymbolId{static_cast<std::uint32_t>(tree.symbols.size()-1-id.value)};};
    const auto scope=[&](ScopeId id){return ScopeId{static_cast<std::uint32_t>(tree.scopes.size()-1-id.value)};};
    for(auto& parameter:tree.parameters)parameter.symbol=symbol(parameter.symbol);
    for(auto& node:tree.expressions) {
      if(auto* read=std::get_if<SymbolExpr>(&node.expression))read->symbol=symbol(read->symbol);
      if(auto* assignment=std::get_if<AssignmentExpr>(&node.expression))std::get<SymbolTarget>(assignment->target).symbol=symbol(std::get<SymbolTarget>(assignment->target).symbol);
    }
    for(auto& node:tree.statements) {
      if(auto* declaration=std::get_if<DeclarationStmt>(&node.statement))declaration->symbol=symbol(declaration->symbol);
      if(auto* block=std::get_if<BlockStmt>(&node.statement))block->scope=scope(block->scope);
    }
    for(auto& node:tree.symbols)node.scope=scope(node.scope);std::reverse(tree.symbols.begin(),tree.symbols.end());
    for(auto& node:tree.scopes)if(node.parent)node.parent=scope(*node.parent);std::reverse(tree.scopes.begin(),tree.scopes.end());
    tree.parameter_scope=scope(tree.parameter_scope);tree.body_scope=scope(tree.body_scope);
  }
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  const bool folded=count&&form!=1&&form!=2&&form!=3&&form!=5&&form!=11&&form!=12&&form!=14&&form!=16&&form!=17&&form!=19;
  test::Check(plan.aliases[reverse?tree.symbols.size()-1-last:last].has_value()==folded);test::Check(!plan.aliases[reverse?tree.symbols.size()-3:2]);
  std::ostringstream output;const auto printed=PrintFunctionTree(tree,output);test::Check(std::holds_alternative<PrintedFunction>(printed));
  for(const auto range:std::get<PrintedFunction>(printed).expressions)test::Check(range.begin<=range.end&&range.end<=output.str().size());
  for(const auto anchor:std::get<PrintedFunction>(printed).anchors)test::Check(anchor>0&&anchor<=output.str().size());
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"alias",form},{"count",count},{"folded",folded}}<<'\n';
  return plan.work;
}
void Decoded(abc::ProfileId profile, unsigned copies, bool split, bool branch, bool emit) {
  using namespace analysis;
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kLDA_V8),2,op(abc::Opcode::kCALLARG0_IMM8),0,op(abc::Opcode::kSTA_V8),0};
  for (unsigned i = 0; i < copies; ++i) bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8),1,0,op(abc::Opcode::kMOV_V8_V8),0,1});
  if (split) bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8),2});
  if (branch) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8),4,op(abc::Opcode::kJEQZ_IMM8),7});
  for (unsigned i = 0; i < 2; ++i) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8),3,op(abc::Opcode::kCALLARG1_IMM8_V8),0,0});
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8),0,op(abc::Opcode::kRETURN)});
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, branch ? 3 : 2, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {200}, {100}, body, frame, InputCaptureMode::kConditional));
  const auto graph = std::get<FunctionValues>(BuildFunctionValues(input, body, frame)).graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {200};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"seed", u"report"}; syntax.parameters = {
      {NameId{0}, NameEvidence::kDebugRange}, {NameId{1}, NameEvidence::kDebugRange}};
  if (branch) { syntax.names.push_back(u"flag"); syntax.parameters.push_back({NameId{2}, NameEvidence::kDebugRange}); }
  std::vector<ExpressionConstant> constants(graph.ops.size()); const ExpressionConstants resolved{constants, syntax.names.size()};
  const auto built = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built)); const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  std::ostringstream output; test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (branch) test::Check(output.str().find("const ") != std::string::npos);
  if (emit) std::cout << nlohmann::json{{"source", output.str()}, {"decoded", true}, {"branch", branch}} << '\n';
}
void DecodedLoop(abc::ProfileId profile, unsigned locals, unsigned copies, bool split, bool emit) {
  using namespace analysis;
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes;
  const auto byte = [&](abc::Opcode code, unsigned operand) { bytes.insert(bytes.end(), {op(code), static_cast<std::uint8_t>(operand)}); };
  constexpr unsigned kBranchBytes = sizeof(std::int32_t);
  const auto jump = [&](abc::Opcode code) {
    const auto at = bytes.size(); bytes.push_back(op(code)); bytes.insert(bytes.end(), kBranchBytes, 0); return at;
  };
  const auto patch = [&](std::size_t from, std::size_t to) {
    const auto delta = static_cast<std::uint32_t>(static_cast<std::int32_t>(to) - static_cast<std::int32_t>(from));
    for (unsigned i = 0; i < kBranchBytes; ++i) bytes[from + 1 + i] = static_cast<std::uint8_t>(delta >> (i * 8));
  };
  const auto boundary = [&] { if (split) { const auto at = jump(abc::Opcode::kJMP_IMM32); patch(at, bytes.size()); } };
  byte(abc::Opcode::kLDA_V8, locals); byte(abc::Opcode::kCALLARG0_IMM8, 0);
  const auto exit = jump(abc::Opcode::kJEQZ_IMM32);
  byte(abc::Opcode::kLDA_V8, locals + 1); byte(abc::Opcode::kCALLARG0_IMM8, 0); byte(abc::Opcode::kSTA_V8, 0);
  for (unsigned i = 0; i < copies; ++i) {
    byte(abc::Opcode::kMOV_V8_V8, 1); bytes.push_back(0);
    byte(abc::Opcode::kMOV_V8_V8, 0); bytes.push_back(1); boundary();
  }
  boundary(); byte(abc::Opcode::kLDA_V8, locals + 3);
  const auto skip = jump(abc::Opcode::kJEQZ_IMM32);
  byte(abc::Opcode::kLDA_V8, locals + 2); byte(abc::Opcode::kCALLARG1_IMM8_V8, 0); bytes.push_back(0);
  patch(skip, bytes.size()); byte(abc::Opcode::kLDA_V8, locals + 2);
  byte(abc::Opcode::kCALLARG1_IMM8_V8, 0); bytes.push_back(0);
  byte(abc::Opcode::kLDA_V8, locals + 4); const auto back = jump(abc::Opcode::kJEQZ_IMM32);
  byte(abc::Opcode::kLDA_V8, 0); bytes.push_back(op(abc::Opcode::kRETURN));
  patch(back, bytes.size()); const auto repeat = jump(abc::Opcode::kJMP_IMM32); patch(repeat, 0);
  patch(exit, bytes.size()); bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(std::span<const std::uint8_t>(bytes).subspan(pc), profile));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta) instruction.branch_target = abc::CodeOffset{static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction); pc += decoded.size;
  }
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(locals, 5, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {201}, {100}, body, frame, InputCaptureMode::kConditional));
  const auto values = BuildFunctionValues(input, body, frame); test::Check(std::holds_alternative<FunctionValues>(values));
  const auto& graph = std::get<FunctionValues>(values).graph;
  const auto control = BuildControlPlan(graph); test::Check(std::holds_alternative<ControlPlan>(control));
  const auto evaluation = BuildEvaluationPlan(graph); test::Check(std::holds_alternative<EvaluationPlan>(evaluation));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {201};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"again", u"seed", u"report", u"choice", u"stop"};
  for (unsigned i = 0; i < frame.parameters; ++i) syntax.parameters.push_back({NameId{i}, NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size()); const ExpressionConstants resolved{constants, syntax.names.size()};
  const auto built = BuildFunctionTree(graph, std::get<ControlPlan>(control), std::get<EvaluationPlan>(evaluation), syntax, resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built)); const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, std::get<ControlPlan>(control), std::get<EvaluationPlan>(evaluation), tree, resolved)));
  const auto loops = std::get<LoopSyntax>(PlanLoopSyntax(tree));
  // The decoded CFG may yield a retained arm or a following native body.
  // Both source ownership forms require a real loop condition, not a shape.
  test::Check(std::any_of(loops.conditions.begin(), loops.conditions.end(), [](const auto& condition) { return bool(condition); }));
  std::ostringstream output; test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  test::Check(output.str().find("const ") != std::string::npos);
  if (emit) std::cout << nlohmann::json{{"source", output.str()}, {"decoded", false}, {"decoded_loop", true},
      {"mutate", locals == 2 && copies == 0 && !split}} << '\n';
}
std::uint64_t Deep(unsigned depth, bool reverse, bool emit) {
  Fixture fixture(8, 1); auto& tree = fixture.tree;
  auto& branch = std::get<IfStmt>(tree.statements[tree.children[std::get<BlockStmt>(tree.statements[tree.body.value].statement).first_child + 1].value].statement);
  auto child = branch.consequent;
  for (unsigned i = 0; i < depth; ++i) {
    const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};
    tree.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
    tree.scopes[std::get<BlockStmt>(tree.statements[child.value].statement).scope.value].parent = scope;
    const auto first = static_cast<std::uint32_t>(tree.children.size()); tree.children.push_back(child);
    child = fixture.Stmt(BlockStmt{scope, first, 1});
  }
  // Resolve again after arena growth; no retained vector element reference.
  const auto branch_id = tree.children[std::get<BlockStmt>(tree.statements[tree.body.value].statement).first_child + 1];
  std::get<IfStmt>(tree.statements[branch_id.value].statement).consequent = child;
  if (reverse) fixture.Reverse();
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops = std::get<LoopSyntax>(PlanLoopSyntax(tree));
  const auto nullish = std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto result = PlanBindingInitializers(tree, loops, nullish);
  test::Check(std::holds_alternative<BindingInitializers>(result));
  const auto& plan = std::get<BindingInitializers>(result);
  test::Check(plan.omitted[fixture.declaration.value] && plan.statements[fixture.store.value]);
  analysis::SemanticBudget exact{plan.allocated_bytes, plan.work};
  test::Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree, loops, nullish, exact)));
  --exact.work;
  test::Check(std::get<TreeStructureError>(PlanBindingInitializers(tree, loops, nullish, exact)) == TreeStructureError::kWorkLimit);
  if (depth <= 201) {
    std::ostringstream output;
    test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
    if (emit) std::cout << nlohmann::json{{"source", output.str()}, {"form", 8}, {"count", 1},
        {"fold", true}, {"decoded", false}, {"depth", depth}} << '\n';
  }
  return plan.work;
}
std::uint64_t NativeDeep(unsigned form, unsigned depth, bool reverse, bool emit) {
  Fixture fixture(form, 1); auto& tree = fixture.tree;
  const auto initial = std::get<LoopSyntax>(PlanLoopSyntax(tree));
  std::optional<StmtId> arm;
  for (const auto& condition : initial.conditions) if (condition && condition->body) arm = condition->body;
  test::Check(arm.has_value());
  const auto block = std::get<BlockStmt>(tree.statements[arm->value].statement);
  const auto first = static_cast<std::uint32_t>(tree.children.size());
  const std::array program{tree.children[block.first_child + 1], tree.children[block.first_child + 2]};
  tree.children.insert(tree.children.end(), program.begin(), program.end());
  const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())}; tree.scopes.push_back({ScopeKind::kBlock, block.scope});
  auto child = fixture.Stmt(BlockStmt{scope, first, 2});
  for (unsigned i = 0; i < depth; ++i) {
    const ScopeId outer{static_cast<std::uint32_t>(tree.scopes.size())}; tree.scopes.push_back({ScopeKind::kBlock, block.scope});
    tree.scopes[std::get<BlockStmt>(tree.statements[child.value].statement).scope.value].parent = outer;
    const auto at = static_cast<std::uint32_t>(tree.children.size()); tree.children.push_back(child);
    child = fixture.Stmt(BlockStmt{outer, at, 1});
  }
  tree.children[block.first_child + 1] = child;
  const ScopeId empty{static_cast<std::uint32_t>(tree.scopes.size())}; tree.scopes.push_back({ScopeKind::kBlock, block.scope});
  tree.children[block.first_child + 2] = fixture.Stmt(BlockStmt{empty, static_cast<std::uint32_t>(tree.children.size()), 0});
  if (reverse) fixture.Reverse();
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops = std::get<LoopSyntax>(PlanLoopSyntax(tree)); const auto nullish = std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto result = PlanBindingInitializers(tree, loops, nullish); test::Check(std::holds_alternative<BindingInitializers>(result));
  const auto& plan = std::get<BindingInitializers>(result);
  test::Check(bool(plan.omitted[fixture.declaration.value]) == (form == 13));
  if (depth <= 201) {
    std::ostringstream output; test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
    if (emit) std::cout << nlohmann::json{{"source", output.str()}, {"form", form}, {"count", 1},
        {"fold", form == 13}, {"decoded", false}, {"depth", depth}} << '\n';
  }
  return plan.work;
}
}
int main(int argc, char**) {
  for(unsigned form=0;form<21;++form) {
    std::uint64_t small=0,large=0;
    for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
      if(!count&&(form==3||form==5||form==14||form==16))continue;
      const auto work=Alias(form,count,reverse,argc>1);if(count==400&&!reverse)small=work;if(count==800&&!reverse)large=work;
    }
    test::Check(large<small*3);
  }
  for (unsigned form = 0;form<16;++form) {
    std::uint64_t small=0,large=0;
    for (unsigned count : {0U,1U,200U,201U,400U,800U}) for (bool reverse : {false,true}) {
      const auto work = Constant(form,count,4,reverse,argc>1);
      if (count==400 && !reverse) small=work;if (count==800 && !reverse) large=work;
    }
    test::Check(large<small*3);
  }
  for (unsigned literal=0;literal<10;++literal) for (bool reverse : {false,true}) Constant(0,1,literal,reverse,argc>1);
  std::array<std::uint64_t, 15> small{}, large{};
  for (unsigned form = 0; form < 15; ++form) for (unsigned count : {0U, 1U, 200U, 201U, 400U, 800U}) for (bool reverse : {false,true}) {
    Fixture fixture(form, count); std::ostringstream output;
    if (reverse) fixture.Reverse();
    const auto loops = std::get<LoopSyntax>(PlanLoopSyntax(fixture.tree));
    if (form == 12) test::Check(std::any_of(loops.transitions.begin(), loops.transitions.end(), [](const auto& transition) { return bool(transition); }));
    if (form >= 13) test::Check(std::any_of(loops.conditions.begin(), loops.conditions.end(), [](const auto& condition) { return condition && condition->body; }));
    const auto nullish = std::get<NullishSyntax>(PlanNullishSyntax(fixture.tree));
    const auto planned = PlanBindingInitializers(fixture.tree, loops, nullish);
    test::Check(std::holds_alternative<BindingInitializers>(planned));
    if (!reverse && count == 400) small[form] = std::get<BindingInitializers>(planned).work;
    if (!reverse && count == 800) large[form] = std::get<BindingInitializers>(planned).work;
    test::Check(std::get<TreeStructureError>(PlanBindingInitializers(fixture.tree, loops, nullish, {0})) == TreeStructureError::kResourceLimit);
    const auto printed = PrintFunctionTree(fixture.tree, output);
    test::Check(std::holds_alternative<PrintedFunction>(printed));
    const auto& ranges = std::get<PrintedFunction>(printed);
    const bool fold = form == 0 || form == 1 || form == 5 || form == 6 || form == 8 || form == 9 || form == 11 || form == 13;
    test::Check((ranges.statements[fixture.declaration.value].begin == ranges.statements[fixture.declaration.value].end) == fold);
    if (fold) test::Check(output.str().find(form == 1 ? "let saved = " : "const saved = ") != std::string::npos);
    for (const auto& at : ranges.expressions) test::Check(at.begin < at.end && at.end <= output.str().size());
    for (auto at : ranges.anchors) test::Check(at > 0 && at <= output.str().size());
    if (argc > 1) std::cout << nlohmann::json{{"source", output.str()}, {"form", form}, {"count", count}, {"fold", fold}, {"decoded", false}} << '\n';
  }
  for (unsigned i = 0; i < small.size(); ++i) test::Check(large[i] < small[i] * 3);
  std::uint64_t deep_small = 0, deep_large = 0;
  for (unsigned depth : {1U, 2U, 200U, 201U, 400U, 800U}) for (bool reverse : {false,true}) {
    const auto work = Deep(depth, reverse, argc > 1);
    if (depth == 400 && !reverse) deep_small = work;
    if (depth == 800 && !reverse) deep_large = work;
  }
  test::Check(deep_large < deep_small * 3);
  for (unsigned form : {13U, 14U}) {
    std::uint64_t small_work = 0, large_work = 0;
    for (unsigned depth : {1U, 2U, 200U, 201U, 400U, 800U}) for (bool reverse : {false,true}) {
      const auto work = NativeDeep(form, depth, reverse, argc > 1);
      if (depth == 400 && !reverse) small_work = work;
      if (depth == 800 && !reverse) large_work = work;
    }
    test::Check(large_work < small_work * 3);
  }
  for (auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (unsigned copies : {0U, 1U, 200U, 201U}) for (bool split : {false, true}) for (bool branch : {false, true})
      Decoded(profile, copies, split, branch, argc > 1);
  for (auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (unsigned locals : {2U, 7U}) for (unsigned copies : {0U, 1U, 200U, 201U}) for (bool split : {false, true})
      DecodedLoop(profile, locals, copies, split, argc > 1);
}
