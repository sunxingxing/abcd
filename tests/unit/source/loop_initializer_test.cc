#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/expression_children.h"
#include "../../../src/source/nullish_syntax.h"
#include "../../../src/source/tail_completion.h"
#include "../../../src/source/discarded_read.h"
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
std::uint64_t Native(unsigned form, unsigned count, bool reverse, bool emit) {
  Fixture fixture(form == 8 ? 1 : 0, count, false); auto& tree = fixture.tree;
  tree.symbols[6].kind = form == 6 ? BindingKind::kLocal : BindingKind::kCompilerTemporary;
  tree.symbols[7].kind = BindingKind::kCompilerTemporary;
  if (form != 6) tree.symbols[6].temporary = TemporaryReason::kPhiJoin;
  tree.symbols[7].temporary = TemporaryReason::kEffectSnapshot;
  // Preserve all expression ownership while replacing the initial source-local
  // value with private undefined storage. The old parameter read is discarded.
  const auto root = std::get<BlockStmt>(tree.statements[tree.body.value].statement);
  const auto old = tree.children[root.first_child];
  const auto initial = *std::get<DeclarationStmt>(tree.statements[old.value].statement).initializer;
  tree.statements[old.value].statement = ExpressionStmt{initial};
  const auto declaration = fixture.Stmt(DeclarationStmt{{6}, {}});
  tree.children.insert(tree.children.begin() + root.first_child, declaration);
  // Stmt() can relocate the statement arena.
  ++std::get<BlockStmt>(tree.statements[tree.body.value].statement).child_count;
  for (auto& statement : tree.statements) if (const auto* returned = std::get_if<ReturnStmt>(&statement.statement); returned && returned->value) {
    if (form == 3 && returned->value == std::get<ReturnStmt>(tree.statements[tree.children.back().value].statement).value) continue;
    tree.expressions[returned->value->value].expression = TreeLiteral{UndefinedLiteral{}};
  }
  for (auto& expression : tree.expressions) if (const auto* call = std::get_if<CallExpr>(&expression.expression)) {
    const auto* callee = std::get_if<SymbolExpr>(&tree.expressions[call->callee.value].expression);
    if (callee && callee->symbol == SymbolId{1} && form == 4)
      std::get<SymbolExpr>(tree.expressions[tree.arguments[call->arguments.first].value].expression).symbol = {6};
  }
  if (form == 1 || form == 2) {
    tree.callable = form == 1 ? CallableKind::kAsyncFunction : CallableKind::kGenerator;
    std::optional<ExprId> store;
    for (unsigned i = 0; i < tree.expressions.size(); ++i) if (const auto* assignment = std::get_if<AssignmentExpr>(&tree.expressions[i].expression))
      if (const auto* target = std::get_if<SymbolTarget>(&assignment->target); target && target->symbol == SymbolId{6}) {
        const auto* call = std::get_if<CallExpr>(&tree.expressions[assignment->value.value].expression);
        if (call && std::get<SymbolExpr>(tree.expressions[call->callee.value].expression).symbol == SymbolId{1}) store = ExprId{i};
      }
    test::Check(store.has_value()); const auto value = std::get<AssignmentExpr>(tree.expressions[store->value].expression).value;
    const auto suspended = form == 1 ? fixture.Expr(AwaitExpr{value}) : fixture.Expr(YieldExpr{value});
    std::get<AssignmentExpr>(tree.expressions[store->value].expression).value = suspended;
  }
  if (form == 5) {
    // A real observation in the entry prefix must retain undefined storage.
    for (auto& expression : tree.expressions) if (auto* assignment = std::get_if<AssignmentExpr>(&expression.expression))
      if (const auto* target = std::get_if<SymbolTarget>(&assignment->target); target && target->symbol == SymbolId{7})
        std::get<SymbolExpr>(tree.expressions[assignment->value.value].expression).symbol = {6};
  }
  if (form == 7) {
    // A declarator list is not limited to a fixed number of SSA copies.
    const auto insert = [&](StmtId owner, unsigned index, const std::vector<StmtId>& items) {
      const auto at = std::get<BlockStmt>(tree.statements[owner.value].statement).first_child + index;
      tree.children.insert(tree.children.begin() + at, items.begin(), items.end());
      for (unsigned i = 0; i < tree.statements.size(); ++i) if (auto* block = std::get_if<BlockStmt>(&tree.statements[i].statement)) {
        if (i == owner.value) block->child_count += items.size();
        else if (block->first_child >= at) block->first_child += items.size();
      }
    };
    std::optional<StmtId> entry;
    unsigned index = 0;
    for (unsigned i = 0; i < tree.statements.size(); ++i) if (const auto* block = std::get_if<BlockStmt>(&tree.statements[i].statement))
      for (unsigned child = 0; child < block->child_count; ++child) if (tree.children[block->first_child + child] == fixture.loop) {entry = StmtId{i}; index = child;}
    test::Check(entry.has_value()); std::vector<StmtId> declarations, stores;
    unsigned previous = 6;
    for (unsigned i = 0; i < count; ++i) {
      const auto symbol = static_cast<std::uint32_t>(tree.symbols.size()), name = static_cast<std::uint32_t>(tree.names.size());
      const auto digits = std::to_string(i); tree.names.push_back(u"copy" + std::u16string(digits.begin(), digits.end()));
      tree.symbols.push_back({{1}, {name}, BindingKind::kCompilerTemporary, DeclarationKind::kLet, NameEvidence::kSynthesized, TemporaryReason::kPhiJoin});
      declarations.push_back(fixture.Stmt(DeclarationStmt{{symbol}, {}})); stores.push_back(fixture.Store(symbol, fixture.Read(previous))); previous = symbol;
    }
    insert(*entry, index, stores);
    insert(tree.body, 0, declarations);
  }
  if (form == 9) {
    tree.labels.push_back(fixture.loop); std::get<LoopStmt>(tree.statements[fixture.loop.value].statement).label = LabelId{0};
    std::optional<StmtId> next;
    for (unsigned i = 0; i < tree.statements.size(); ++i) if (std::holds_alternative<ContinueStmt>(tree.statements[i].statement)) next = StmtId{i};
    test::Check(next.has_value());
    const auto body = std::get<LoopStmt>(tree.statements[fixture.loop.value].statement).body;
    const auto parent = std::get<BlockStmt>(tree.statements[body.value].statement).scope;
    const auto nested = fixture.Block(fixture.Scope(parent), {fixture.Stmt(ContinueStmt{fixture.loop, LabelId{0}})});
    tree.statements[next->value].statement = LoopStmt{nested, {}};
  }
  tree.anchors.clear(); tree.operations.clear(); if (reverse) fixture.Reverse();
  for (unsigned i = 0; i < tree.statements.size(); ++i) if (const auto* block = std::get_if<BlockStmt>(&tree.statements[i].statement))
    for (unsigned child = 0; child <= block->child_count; ++child) {
      const auto anchor = static_cast<std::uint32_t>(tree.anchors.size());
      tree.anchors.push_back({BlockBoundary{{i}, child}, {0}}); tree.operations.push_back({{anchor}, AnchorId{anchor}});
    }
  const auto structure = VerifyTreeStructure(tree);
  test::Check(std::holds_alternative<TreeStructure>(structure));
  const auto loops = std::get<LoopSyntax>(PlanLoopSyntax(tree));
  const auto nullish = std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto planned = PlanBindingInitializers(tree, loops, nullish);test::Check(std::holds_alternative<BindingInitializers>(planned));
  const auto& bindings = std::get<BindingInitializers>(planned);
  std::ostringstream source; const auto printed = PrintFunctionTree(tree, source); test::Check(std::holds_alternative<PrintedFunction>(printed));
  const bool lexical = form < 3 || form >= 6;
  test::Check((source.str().find("for (let state = ") != std::string::npos) == lexical);
  const auto& ranges = std::get<PrintedFunction>(printed);
  const auto completion = ImplicitTailCompletion(tree);
  std::vector<bool> elided(tree.expressions.size());
  if (completion && completion->value) elided[completion->value->value] = true;
  std::vector<ExprId> pending;
  for(unsigned i=0;i<tree.statements.size();++i)if(bindings.omitted[i])
    if(const auto* effect=std::get_if<ExpressionStmt>(&tree.statements[i].statement)) {
      if(bindings.forwarded_statements[i])elided[effect->expression.value]=true;
      else pending.push_back(effect->expression);
    }
  while(!pending.empty()) {
    const auto id=pending.back();pending.pop_back();if(elided[id.value])continue;
    elided[id.value]=true;VisitExpressionChildren(tree,tree.expressions[id.value].expression,
        [&](ExprId child){pending.push_back(child);});
    if(const auto replacement=bindings.replacements[id.value])pending.push_back(*replacement);
  }
  for (unsigned i = 0; i < tree.statements.size(); ++i) if (const auto read = DiscardedInvocationRead(tree, StmtId{i})) elided[read->value] = true;
  for (unsigned i = 0; i < ranges.expressions.size(); ++i) {
    const auto range = ranges.expressions[i];
    test::Check(range.begin <= range.end && range.end <= source.str().size());
    if (range.begin == range.end) {
      if(!elided[i]||!range.begin)std::cerr<<"binding="<<form<<" count="<<count<<" reverse="<<reverse<<" expr="<<i<<" kind="<<tree.expressions[i].expression.index()<<" replacement="<<bindings.replacements[i].has_value()<<" at="<<range.begin<<'\n';
      test::Check(elided[i] && range.begin > 0);
    }
  }
  for (const auto anchor : ranges.anchors) test::Check(anchor > 0 && anchor <= source.str().size());
  if (emit) std::cout << nlohmann::json{{"source", source.str()}, {"lexical", lexical}, {"binding", form}, {"count", count}} << '\n';
  return bindings.work;
}
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
  for (unsigned form = 0; form < 10; ++form) {
    std::uint64_t small = 0, large = 0;
    for (unsigned count : {0U,1U,200U,201U,400U,800U}) for (bool reverse : {false,true}) {
      const auto work = Native(form, count, reverse, argc > 1);
      if (count == 400 && !reverse) small = work;if (count == 800 && !reverse) large = work;
    }
    test::Check(large < small * 3);
  }
}
