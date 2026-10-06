#include "../../../src/source/loop_syntax.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <algorithm>
#include <array>
#include <sstream>
#include <nlohmann/json.hpp>
using namespace abcd;
using namespace abcd::source;
namespace {
// Independent source trees exercise entry ownership, not a bytecode layout.
struct Fixture {
  FunctionTree tree{}; StmtId loop{1}; std::optional<StmtId> outside;
  ExprId Expr(TreeExpression node) {
    const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())}; tree.expressions.push_back({std::move(node), {0}}); return id;
  }
  StmtId Stmt(TreeStatement node) {
    const StmtId id{static_cast<std::uint32_t>(tree.statements.size())}; tree.statements.push_back({std::move(node), {0}}); return id;
  }
  ScopeId Scope(ScopeId parent, ScopeKind kind = ScopeKind::kBlock) {
    const ScopeId id{static_cast<std::uint32_t>(tree.scopes.size())}; tree.scopes.push_back({kind, parent}); return id;
  }
  StmtId Block(ScopeId scope, const std::vector<StmtId>& children) {
    const auto first = static_cast<std::uint32_t>(tree.children.size()); tree.children.insert(tree.children.end(), children.begin(), children.end());
    return Stmt(BlockStmt{scope, first, static_cast<std::uint32_t>(children.size()), {}});
  }
  ExprId Read(unsigned symbol) { return Expr(SymbolExpr{{symbol}}); }
  ExprId Call(unsigned symbol, std::optional<unsigned> argument = {}) {
    const auto first = static_cast<std::uint32_t>(tree.arguments.size()); if (argument) tree.arguments.push_back(Read(*argument));
    return Expr(CallExpr{Read(symbol), {first, argument ? 1U : 0U}, CallReference::kValue});
  }
  StmtId Store(unsigned symbol, ExprId value) { return Stmt(ExpressionStmt{Expr(AssignmentExpr{SymbolTarget{{symbol}}, value})}); }
  Fixture(unsigned form, unsigned count, bool reverse) {
    tree.callable = form == 3 ? CallableKind::kAsyncFunction : form == 4 ? CallableKind::kGenerator : CallableKind::kFunction;
    tree.strictness = Strictness::kStrict; tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.names = {u"initial", u"setup", u"report", u"step", u"mark", u"choose", u"state", u"next", u"error", u"local", u"gate"};
    tree.origins = {OriginalSite{{0}, {0}}};
    for (unsigned i = 0; i < 6; ++i) {
      tree.symbols.push_back({{0}, {i}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
      tree.parameters.push_back({{i}, {}, false});
    }
    for (unsigned i : {6U, 7U}) tree.symbols.push_back({{1}, {i}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
    if (form == 11) tree.symbols.push_back({{1}, {10}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
    tree.statements = {{BlockStmt{{1}, 0, 0, {}}, {0}}, {LoopStmt{{0}, {}}, {0}}};
    const auto protected_scope = Scope({1});
    auto entry_scope = protected_scope;
    if (form == 1) for (unsigned i = 0; i <= count; ++i) entry_scope = Scope(entry_scope);
    if (form == 5 || form == 10) entry_scope = Scope(protected_scope);
    const auto scope = Scope(entry_scope);
    auto test = Read(6); if (form == 11) test = Expr(LogicalExpr{TreeLogical::kAnd, test, Read(8)});
    std::vector<StmtId> body{Stmt(IfStmt{Expr(UnaryExpr{TreeUnary::kNot, test}), Block(Scope(scope), {Stmt(BreakStmt{loop, {}})}), {}})};
    for (unsigned i = 0; i <= (form == 12 ? 0 : count); ++i) body.push_back(Stmt(ExpressionStmt{Call(2, 6)}));
    if (form == 8) {
      body.push_back(Stmt(IfStmt{Call(5), Block(Scope(scope), {Store(6, Call(3, 6)), Stmt(ContinueStmt{loop, {}})}), {}}));
      body.push_back(Stmt(TryStmt{Block(Scope(scope), {Stmt(ExpressionStmt{Call(4)})}), {}, Block(Scope(scope), {Stmt(ExpressionStmt{Call(4)})})}));
    }
    body.push_back(Store(6, Call(3, 6))); body.push_back(Stmt(ContinueStmt{loop, {}}));
    std::get<LoopStmt>(tree.statements[loop.value].statement).body = Block(scope, body);
    std::vector<StmtId> entry{Store(7, Read(0)), Stmt(ExpressionStmt{Call(4)})};
    if (form == 11) { outside = Store(8, Call(4)); entry.insert(entry.begin(), *outside); }
    std::vector<StmtId> locals; unsigned argument = 7;
    if (form == 12) for (unsigned i = 0; i < count; ++i) {
      const auto symbol = static_cast<std::uint32_t>(tree.symbols.size()), name = static_cast<std::uint32_t>(tree.names.size());
      const auto digits = std::to_string(i); tree.names.push_back(u"chain" + std::u16string(digits.begin(), digits.end()));
      tree.symbols.push_back({{1}, {name}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
      locals.push_back(Stmt(DeclarationStmt{{symbol}, {}}));
      entry.push_back(Store(symbol, Read(argument))); entry.push_back(Stmt(ExpressionStmt{Call(4)})); argument = symbol;
    }
    std::optional<ExprId> setup;
    if (form != 9) setup = Call(1, argument);
    if (form == 3) setup = Expr(AwaitExpr{*setup}); if (form == 4) setup = Expr(YieldExpr{*setup});
    if (form == 2) entry.push_back(Stmt(IfStmt{Call(5), Block(Scope(entry_scope), {Store(6, *setup)}), Block(Scope(entry_scope), {Store(6, Read(7))})}));
    else if (form == 9) {
      const auto local = static_cast<std::uint32_t>(tree.symbols.size());
      tree.symbols.push_back({protected_scope, {9}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
      entry.push_back(Store(6, Read(local))); entry.push_back(Stmt(DeclarationStmt{{local}, Read(0)}));
    } else entry.push_back(Store(6, *setup));
    if (form == 7) {
      const auto local = static_cast<std::uint32_t>(tree.symbols.size());
      tree.symbols.push_back({protected_scope, {9}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
      entry.push_back(Stmt(DeclarationStmt{{local}, Call(4)}));
    }
    std::vector<StmtId> protected_body;
    if (form == 1) {
      // Initial roots and the loop are in different transparent blocks.
      auto inner = Block(entry_scope, {entry[1], entry[2], loop});
      for (auto parent = *tree.scopes[entry_scope.value].parent; parent != protected_scope; parent = *tree.scopes[parent.value].parent)
        inner = Block(parent, {inner});
      protected_body = {entry[0], inner};
    } else if (form == 5 || form == 10) {
      std::vector<StmtId> nested;
      if (form == 5) {
        const auto local = static_cast<std::uint32_t>(tree.symbols.size());
        tree.symbols.push_back({entry_scope, {9}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
        nested.push_back(Stmt(DeclarationStmt{{local}, {}}));
      }
      nested.push_back(loop); const auto inner = Block(entry_scope, nested);
      if (form == 10) {
        tree.labels.push_back(inner); tree.children.push_back(Stmt(BreakStmt{inner, LabelId{0}}));
        auto& block = std::get<BlockStmt>(tree.statements[inner.value].statement); block.label = LabelId{0}; ++block.child_count;
      }
      protected_body = entry; protected_body.push_back(inner);
    } else { protected_body = entry; protected_body.push_back(loop); }
    const auto catch_scope = Scope({1}, ScopeKind::kCatch);
    const auto caught = static_cast<std::uint32_t>(tree.symbols.size());
    tree.symbols.push_back({catch_scope, {8}, BindingKind::kCatchParameter, DeclarationKind::kCatchParameter, NameEvidence::kSynthesized, {}});
    if (form == 6) protected_body = {loop};
    const auto region = Stmt(TryStmt{Block(protected_scope, protected_body), CatchClause{{caught}, Block(catch_scope, {Stmt(ReturnStmt{Read(6)})})}, {}});
    std::vector<StmtId> roots{Stmt(DeclarationStmt{{6}, Read(0)}), Stmt(DeclarationStmt{{7}, {}})};
    roots.insert(roots.end(), locals.begin(), locals.end());
    if (form == 11) roots.push_back(Stmt(DeclarationStmt{{8}, {}}));
    if (form == 6) roots.insert(roots.end(), entry.begin(), entry.end());
    roots.push_back(region); roots.push_back(Stmt(ReturnStmt{Read(6)}));
    const auto first = static_cast<std::uint32_t>(tree.children.size()); tree.children.insert(tree.children.end(), roots.begin(), roots.end());
    std::get<BlockStmt>(tree.statements[0].statement) = {{1}, first, static_cast<std::uint32_t>(roots.size()), {}};
    if (reverse) Reverse();
    for (std::uint32_t s = 0; s < tree.statements.size(); ++s)
      if (const auto* block = std::get_if<BlockStmt>(&tree.statements[s].statement))
        for (std::uint32_t i = 0; i <= block->child_count; ++i) {
          const auto index = static_cast<std::uint32_t>(tree.anchors.size()); tree.anchors.push_back({BlockBoundary{{s}, i}, {0}});
          tree.operations.push_back({{index}, AnchorId{index}});
        }
  }
  void Reverse() {
    const auto total = static_cast<std::uint32_t>(tree.statements.size()); const auto remap = [&](StmtId id) { return StmtId{total - 1 - id.value}; };
    for (auto& child : tree.children) child = remap(child);
    for (auto& label : tree.labels) label = remap(label);
    for (auto& statement : tree.statements) {
      if (auto* node = std::get_if<LoopStmt>(&statement.statement)) node->body = remap(node->body);
      if (auto* node = std::get_if<IfStmt>(&statement.statement)) { node->consequent = remap(node->consequent); if (node->alternate) node->alternate = remap(*node->alternate); }
      if (auto* node = std::get_if<TryStmt>(&statement.statement)) { node->body = remap(node->body); if (node->handler) node->handler->body = remap(node->handler->body); if (node->finalizer) node->finalizer = remap(*node->finalizer); }
      if (auto* node = std::get_if<BreakStmt>(&statement.statement)) node->target = remap(node->target);
      if (auto* node = std::get_if<ContinueStmt>(&statement.statement)) node->target = remap(node->target);
    }
    tree.body = remap(tree.body); loop = remap(loop); if (outside) outside = remap(*outside); std::reverse(tree.statements.begin(), tree.statements.end());
  }
};
}
int main(int argc, char**) {
  std::array<std::uint64_t, 3> small{}, large{};
  for (unsigned form = 0; form < 13; ++form) for (unsigned count : {0U, 1U, 200U, 201U, 400U, 800U}) for (bool reverse : {false, true}) {
    Fixture fixture(form, count, reverse); const auto planned = PlanLoopSyntax(fixture.tree);
    test::Check(std::holds_alternative<LoopSyntax>(planned)); const auto& plan = std::get<LoopSyntax>(planned);
    const bool native = form < 5 || form == 8 || form == 11 || form == 12; test::Check(bool(plan.initializers[fixture.loop.value]) == native);
    if (fixture.outside) test::Check(!plan.initializer_owners[fixture.outside->value]);
    if ((form < 2 || form == 12) && !reverse) {
      const auto sample = form == 12 ? 2 : form;
      if (count == 400) small[sample] = plan.work; if (count == 800) large[sample] = plan.work;
    }
    test::Check(std::get<TreeStructureError>(PlanLoopSyntax(fixture.tree, {0})) == TreeStructureError::kResourceLimit);
    std::ostringstream source; const auto printed = PrintFunctionTree(fixture.tree, source); test::Check(std::holds_alternative<PrintedFunction>(printed));
    const auto code = source.str(); const auto& ranges = std::get<PrintedFunction>(printed);
    for (const auto& range : ranges.expressions) test::Check(range.begin < range.end && range.end <= code.size());
    for (const auto& range : ranges.statements) test::Check(range.begin <= range.end && range.end <= code.size());
    for (const auto anchor : ranges.anchors) test::Check(anchor > 0 && anchor <= code.size());
    if (argc > 1) std::cout << nlohmann::json{{"source", code}, {"form", form}, {"count", count}, {"native", native}} << '\n';
  }
  for (unsigned i = 0; i < small.size(); ++i) test::Check(large[i] < small[i] * 3);
}
