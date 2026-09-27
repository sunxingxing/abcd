#include "../../../src/source/tree_structure.h"
#include "../../../src/source/family_print.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string_view>

namespace {
using namespace abcd;
using namespace abcd::source;
constexpr std::int32_t kCatchReplacement = 17;
void Check(bool condition) { if (!condition) std::abort(); }

// Syntax fixtures, not ABC equivalence certificates. Every expression is an
// owned occurrence, including repeated reads of the same catch parameter.
FunctionTree CatchTree() {
  FunctionTree tree{};
  tree.callable = CallableKind::kFunction;
  tree.strictness = Strictness::kStrict;
  tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
  tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}},
      {ScopeKind::kBlock, ScopeId{1}}, {ScopeKind::kCatch, ScopeId{1}},
      {ScopeKind::kBlock, ScopeId{1}}};
  tree.names = {u"value", u"value", u"finish"};
  tree.symbols = {
      {{0}, {0}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kMetadata, {}},
      {{3}, {1}, BindingKind::kCatchParameter, DeclarationKind::kCatchParameter, NameEvidence::kSynthesized, {}},
      {{0}, {2}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kMetadata, {}}};
  tree.parameters = {{{0}, {}, false}, {{2}, {}, false}};
  tree.origins.emplace_back(OriginalSite{{0}, {0}});
  tree.expressions = {{SymbolExpr{{0}}, {0}}, {SymbolExpr{{1}}, {0}},
      {SymbolExpr{{2}}, {0}}, {CallExpr{{2}, {0, 0}, CallReference::kValue}, {0}}};
  tree.statements = {
      {BlockStmt{{1}, 0, 1}, {0}},
      {TryStmt{{2}, CatchClause{{1}, {4}}, StmtId{6}}, {0}},
      {BlockStmt{{2}, 1, 1}, {0}}, {ThrowStmt{{0}}, {0}},
      {BlockStmt{{3}, 2, 1}, {0}}, {ReturnStmt{ExprId{1}}, {0}},
      {BlockStmt{{4}, 3, 1}, {0}}, {ExpressionStmt{{3}}, {0}}};
  tree.children = {{1}, {3}, {5}, {7}};
  return tree;
}
void Expect(const FunctionTree& tree, TreeStructureError expected) {
  const auto checked = VerifyTreeStructure(tree);
  Check(std::holds_alternative<TreeStructureError>(checked));
  Check(std::get<TreeStructureError>(checked) == expected);
}
std::string Print(const FunctionTree& tree) {
  const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
  std::ostringstream output;
  const auto printed = std::get<PrintedFamilyDraft>(PrintFamilyDraft(family, output));
  const auto source = output.str();
  for (const auto& range : printed.members[0].statements)
    Check(range.begin < range.end && range.end <= source.size());
  const auto range = printed.members[0].statements[1];
  Check(source.substr(range.begin, range.end - range.begin).starts_with("try {"));
  for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
    const auto range = printed.members[0].expressions[i];
    Check(range.begin < range.end && range.end <= source.size());
    if (const auto* symbol = std::get_if<SymbolExpr>(&tree.expressions[i].expression)) {
      const auto& name = tree.names[tree.symbols[symbol->symbol.value].name.value];
      Check(source.substr(range.begin, range.end - range.begin) == std::string(name.begin(), name.end()));
    }
  }
  return source;
}
void Scopes() {
  auto tree = CatchTree();
  const auto checked = std::get<TreeStructure>(VerifyTreeStructure(tree));
  Check(checked.expression_scopes == std::vector<ScopeId>{{2}, {3}, {4}, {4}});
  Check(Print(tree).find("catch (value)") != std::string::npos);
  tree.expressions[0].expression = SymbolExpr{{1}};
  Expect(tree, TreeStructureError::kScope); // Protected body cannot see handler binding.
  tree = CatchTree(); tree.expressions[1].expression = SymbolExpr{{0}};
  Expect(tree, TreeStructureError::kScope); // Same spelling resolves to catch, not formal.
  tree = CatchTree(); tree.expressions[2].expression = SymbolExpr{{1}};
  Expect(tree, TreeStructureError::kScope); // Finally is outside the catch environment.
  tree = CatchTree(); tree.scopes[3].kind = ScopeKind::kBlock;
  Expect(tree, TreeStructureError::kBinding);
  tree = CatchTree(); std::get<TryStmt>(tree.statements[1].statement).handler->parameter = {0};
  Expect(tree, TreeStructureError::kBinding);
  tree = CatchTree(); std::get<TryStmt>(tree.statements[1].statement).handler->parameter = {99};
  Expect(tree, TreeStructureError::kRange);
  tree = CatchTree(); tree.symbols[1].declaration = DeclarationKind::kLet;
  Expect(tree, TreeStructureError::kBinding);
  tree = CatchTree(); tree.statements[5].statement = DeclarationStmt{{1}, ExprId{1}};
  Expect(tree, TreeStructureError::kBinding); // No re-declaration as an ordinary local.
  tree = CatchTree(); tree.symbols.push_back({{3}, {0}, BindingKind::kLocal,
      DeclarationKind::kLet, NameEvidence::kMetadata, {}});
  Expect(tree, TreeStructureError::kName);
  tree = CatchTree(); tree.scopes[4].kind = ScopeKind::kCatch;
  Expect(tree, TreeStructureError::kScope); // Only an explicit handler enters a catch scope.
  tree = CatchTree(); std::get<TryStmt>(tree.statements[1].statement).handler->body = {3};
  Expect(tree, TreeStructureError::kScope);
  tree = CatchTree(); auto& region = std::get<TryStmt>(tree.statements[1].statement);
  region.handler.reset(); region.finalizer.reset();
  Expect(tree, TreeStructureError::kUnsupportedSyntax);
  tree = CatchTree(); std::get<TryStmt>(tree.statements[1].statement).finalizer = StmtId{2};
  Expect(tree, TreeStructureError::kOwnership);
}
void HandlerShadowing() {
  auto tree = CatchTree();
  const ScopeId nested{static_cast<std::uint32_t>(tree.scopes.size())};
  const SymbolId shadow{static_cast<std::uint32_t>(tree.symbols.size())};
  const StmtId block{static_cast<std::uint32_t>(tree.statements.size())};
  const ExprId value{static_cast<std::uint32_t>(tree.expressions.size())};
  tree.scopes.push_back({ScopeKind::kBlock, ScopeId{3}});
  tree.symbols.push_back({nested, {1}, BindingKind::kLocal, DeclarationKind::kLet,
      NameEvidence::kSynthesized, {}});
  tree.expressions.push_back({SymbolExpr{shadow}, {0}});
  tree.statements.push_back({BlockStmt{nested, 5, 2}, {0}});
  tree.statements.push_back({DeclarationStmt{shadow, {}}, {0}});
  tree.statements.push_back({ExpressionStmt{value}, {0}});
  tree.children = {{1}, {3}, block, {5}, {7}, {block.value + 1}, {block.value + 2}};
  // Handler has a nested lexical shadow, followed by a reference to the
  // restored catch parameter. Keep every child slot exclusively owned.
  std::get<BlockStmt>(tree.statements[4].statement).child_count = 2;
  std::get<BlockStmt>(tree.statements[6].statement).first_child = 4;
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  Print(tree);
  tree.expressions[value.value].expression = SymbolExpr{{1}};
  Expect(tree, TreeStructureError::kScope); // Inner spelling cannot resolve to outer catch.
}
void Nested() {
  for (const std::uint32_t depth : {1U, 200U, 201U, 402U}) {
    FunctionTree tree{};
    tree.callable = CallableKind::kFunction; tree.strictness = Strictness::kStrict;
    tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.origins.emplace_back(OriginalSite{{0}, {0}});
    tree.statements.push_back({BlockStmt{{1}, 0, 1}, {0}});
    tree.children.push_back({1});
    ScopeId parent{1};
    for (std::uint32_t i = 0; i < depth; ++i) {
      const auto stmt = static_cast<std::uint32_t>(tree.statements.size());
      const auto scope = static_cast<std::uint32_t>(tree.scopes.size());
      const auto child = static_cast<std::uint32_t>(tree.children.size());
      tree.scopes.push_back({ScopeKind::kBlock, parent});
      tree.scopes.push_back({ScopeKind::kBlock, parent});
      tree.statements.push_back({TryStmt{{stmt + 1}, {}, StmtId{stmt + 2}}, {0}});
      tree.statements.push_back({BlockStmt{{scope}, child, 1}, {0}});
      tree.statements.push_back({BlockStmt{{scope + 1}, child, 0}, {0}});
      tree.children.push_back({stmt + 3});
      parent = {scope};
    }
    tree.statements.push_back({ReturnStmt{}, {0}});
    const auto checked = std::get<TreeStructure>(VerifyTreeStructure(tree));
    Check(checked.maximum_depth == 2 * depth + 2);
    analysis::SemanticBudget budget;
    budget.bytes = checked.allocated_bytes; budget.work = checked.work;
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree, budget)));
    --budget.work;
    Check(std::get<TreeStructureError>(VerifyTreeStructure(tree, budget)) == TreeStructureError::kWorkLimit);
    ++budget.work; --budget.bytes;
    Check(std::get<TreeStructureError>(VerifyTreeStructure(tree, budget)) == TreeStructureError::kResourceLimit);
  }
}
FunctionTree MutableCatch() {
  auto tree = CatchTree();
  tree.expressions[1].expression = TreeLiteral{IntegerLiteral{kCatchReplacement}};
  const ExprId assignment{static_cast<std::uint32_t>(tree.expressions.size())};
  tree.expressions.push_back({AssignmentExpr{SymbolTarget{{1}}, {1}}, {0}});
  tree.statements[5].statement = ReturnStmt{assignment};
  return tree;
}
FunctionTree RestoredScope() {
  auto tree = CatchTree();
  tree.statements[5].statement = ExpressionStmt{{1}};
  const StmtId returned{static_cast<std::uint32_t>(tree.statements.size())};
  const ExprId value{static_cast<std::uint32_t>(tree.expressions.size())};
  tree.expressions.push_back({SymbolExpr{{0}}, {0}});
  const ExprId replacement{static_cast<std::uint32_t>(tree.expressions.size())};
  tree.expressions.push_back({TreeLiteral{IntegerLiteral{kCatchReplacement}}, {0}});
  tree.expressions[1].expression = AssignmentExpr{SymbolTarget{{1}}, replacement};
  tree.statements.push_back({ReturnStmt{value}, {0}});
  tree.statements[0].statement = BlockStmt{{1}, 0, 2};
  tree.children = {{1}, returned, {3}, {5}, {7}};
  std::get<BlockStmt>(tree.statements[2].statement).first_child = 2;
  std::get<BlockStmt>(tree.statements[4].statement).first_child = 3;
  std::get<BlockStmt>(tree.statements[6].statement).first_child = 4;
  return tree;
}
FunctionTree CatchOnly() {
  auto tree = CatchTree();
  std::get<TryStmt>(tree.statements[1].statement).finalizer.reset();
  tree.scopes.resize(4); tree.expressions.resize(2);
  tree.statements.resize(6); tree.children.resize(3);
  return tree;
}
FunctionTree FinallyOnly() {
  auto tree = CatchTree();
  tree.scopes.resize(4); tree.scopes[3].kind = ScopeKind::kBlock;
  tree.symbols.erase(tree.symbols.begin() + 1);
  tree.parameters[1].symbol = {1};
  tree.expressions = {{SymbolExpr{{0}}, {0}}, {SymbolExpr{{1}}, {0}},
      {CallExpr{{1}, {0, 0}, CallReference::kValue}, {0}}};
  tree.statements.resize(6);
  tree.statements[1].statement = TryStmt{{2}, {}, StmtId{4}};
  tree.statements[5].statement = ExpressionStmt{{2}};
  tree.children.resize(3);
  return tree;
}
FunctionTree LoopFinally() {
  auto tree = FinallyOnly();
  const StmtId loop{static_cast<std::uint32_t>(tree.statements.size())};
  const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};
  const StmtId block{loop.value + 1};
  const StmtId returned{loop.value + 2};
  tree.scopes.push_back({ScopeKind::kBlock, tree.body_scope});
  tree.scopes[2].parent = scope; tree.scopes[3].parent = scope;
  tree.statements[3].statement = BreakStmt{loop};
  tree.statements.push_back({LoopStmt{block}, {0}});
  tree.statements.push_back({BlockStmt{scope, 2, 1}, {0}});
  tree.statements.push_back({ReturnStmt{ExprId{0}}, {0}});
  tree.statements[0].statement = BlockStmt{tree.body_scope, 0, 2};
  std::get<BlockStmt>(tree.statements[2].statement).first_child = 3;
  std::get<BlockStmt>(tree.statements[4].statement).first_child = 4;
  tree.children = {loop, returned, {1}, {3}, {5}};
  return tree;
}
}  // namespace
int main(int argc, char** argv) {
  Scopes(); HandlerShadowing(); Nested();
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(MutableCatch())));
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(RestoredScope())));
  Print(CatchOnly()); Print(FinallyOnly()); Print(LoopFinally());
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    std::cout << "export const catchReplacement = " << kCatchReplacement << ";\n";
    std::cout << "export const caught = " << Print(CatchTree()) << ";\n";
    auto overridden = CatchTree();
    overridden.statements[7].statement = ReturnStmt{ExprId{3}};
    std::cout << "export const overridden = " << Print(overridden) << ";\n";
    auto rethrown = CatchTree();
    rethrown.statements[5].statement = ThrowStmt{{1}};
    std::cout << "export const rethrown = " << Print(rethrown) << ";\n";
    std::cout << "export const mutableCatch = " << Print(MutableCatch()) << ";\n";
    std::cout << "export const restored = " << Print(RestoredScope()) << ";\n";
    std::cout << "export const catchOnly = " << Print(CatchOnly()) << ";\n";
    std::cout << "export const finallyOnly = " << Print(FinallyOnly()) << ";\n";
    auto loop = LoopFinally();
    std::cout << "export const breakFinally = " << Print(loop) << ";\n";
    loop.statements[3].statement = ContinueStmt{std::get<BreakStmt>(loop.statements[3].statement).target};
    loop.statements[5].statement = ReturnStmt{ExprId{2}};
    std::cout << "export const continueOverride = " << Print(loop) << ";\n";
  }
}
