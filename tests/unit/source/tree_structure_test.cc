#include "../../../src/source/tree_structure.h"

#include <cstdlib>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
FunctionTree Minimal() {
  FunctionTree tree{};
  tree.callable = CallableKind::kFunction;
  tree.strictness = Strictness::kStrict;
  tree.parameter_scope = {0};
  tree.body_scope = {1};
  tree.body = {0};
  tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
  tree.origins.emplace_back(OriginalSite{{0}, {0}});
  tree.expressions.push_back({TreeLiteral{UndefinedLiteral{}}, {0}});
  tree.statements.push_back({BlockStmt{{1}, 0, 1}, {0}});
  tree.statements.push_back({ReturnStmt{ExprId{0}}, {0}});
  tree.children.push_back({1});
  return tree;
}
void Expect(const FunctionTree& tree, TreeStructureError expected) {
  const auto checked = VerifyTreeStructure(tree);
  Check(std::holds_alternative<TreeStructureError>(checked));
  Check(std::get<TreeStructureError>(checked) == expected);
}
void Ownership() {
  auto tree = Minimal();
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  tree.expressions.push_back({BinaryExpr{analysis::BinaryOperator::kAdd, {0}, {0}}, {0}});
  tree.statements[1].statement = ReturnStmt{ExprId{1}};
  Expect(tree, TreeStructureError::kOwnership);
  tree = Minimal();
  tree.expressions[0].expression = UnaryExpr{TreeUnary::kNot, {0}};
  Expect(tree, TreeStructureError::kOwnership);
  tree = Minimal();
  tree.expressions.push_back({TreeLiteral{NullLiteral{}}, {0}});
  Expect(tree, TreeStructureError::kOwnership);
  tree = Minimal();
  tree.children.push_back({1});
  std::get<BlockStmt>(tree.statements[0].statement).child_count = 2;
  Expect(tree, TreeStructureError::kOwnership);
  tree = Minimal();
  tree.children.push_back({1});
  Expect(tree, TreeStructureError::kOwnership);
}
FunctionTree Shadowed() {
  auto tree = Minimal();
  tree.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
  tree.names = {u"value", u"value"};
  tree.symbols = {{{1}, {0}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kDebugRange, {}},
                  {{2}, {1}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kDebugRange, {}}};
  tree.expressions[0].expression = SymbolExpr{{1}};
  tree.statements[0].statement = BlockStmt{{1}, 0, 2};
  tree.statements.push_back({DeclarationStmt{{0}, {}}, {0}});
  tree.statements.push_back({BlockStmt{{2}, 2, 2}, {0}});
  tree.statements.push_back({DeclarationStmt{{1}, {}}, {0}});
  tree.children = {{2}, {3}, {4}, {1}};
  return tree;
}
void ScopesAndBindings() {
  auto tree = Shadowed();
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  tree.expressions[0].expression = SymbolExpr{{0}};
  Expect(tree, TreeStructureError::kScope);
  tree = Shadowed();
  tree.scopes[2].parent = ScopeId{0};
  Expect(tree, TreeStructureError::kScope);
  tree = Shadowed();
  tree.symbols[1].scope = {1};
  Expect(tree, TreeStructureError::kName);
  tree = Shadowed();
  tree.symbols[1].declaration = DeclarationKind::kConst;
  Expect(tree, TreeStructureError::kBinding);
  tree = Shadowed();
  tree.names[1] = u"await";
  Expect(tree, TreeStructureError::kName);
  tree = Minimal();
  tree.expressions[0].expression = SymbolExpr{{0}};
  Expect(tree, TreeStructureError::kRange);
  tree = Minimal();
  tree.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
  Expect(tree, TreeStructureError::kScope);
  tree = Minimal();
  tree.names.push_back(u"param");
  tree.symbols.push_back({{0}, {0}, BindingKind::kParameter,
                          DeclarationKind::kParameter, NameEvidence::kMetadata, {}});
  Expect(tree, TreeStructureError::kBinding);
  tree.parameters.push_back({{0}, {}, false});
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  tree.parameters[0].rest = true;
  Expect(tree, TreeStructureError::kUnsupportedSyntax);
}
void OriginsAndAnchors() {
  auto tree = Minimal();
  tree.origins.emplace_back(CombinedOrigin{0, 2});
  tree.origin_parts = {{0}, {1}};
  Expect(tree, TreeStructureError::kOrigin);
  tree.origin_parts[1] = {0};
  tree.expressions[0].origin = {1};
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  tree.origin_parts.push_back({0});
  Expect(tree, TreeStructureError::kOrigin);
  tree = Minimal();
  tree.anchors.push_back({BlockBoundary{{0}, 1}, {0}});
  Expect(tree, TreeStructureError::kOwnership);
  tree.operations.push_back({{0}, AnchorId{0}});
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  std::get<BlockBoundary>(tree.anchors[0].position).index = 2;
  Expect(tree, TreeStructureError::kOrigin);
  tree.anchors[0].position = BlockBoundary{{1}, 0};
  Expect(tree, TreeStructureError::kOrigin);
  tree = Minimal();
  tree.operations = {{{0}, ExprId{0}}, {{0}, StmtId{1}}};
  Expect(tree, TreeStructureError::kOwnership);
  tree.operations = {{{0}, ExprId{1}}};
  Expect(tree, TreeStructureError::kRange);
}
void DeepTrees() {
  for (const std::uint32_t size : {200U, 201U, 4096U}) {
    auto tree = Minimal();
    for (std::uint32_t i = 1; i < size; ++i)
      tree.expressions.push_back({UnaryExpr{TreeUnary::kNot, {i - 1}}, {0}});
    tree.statements[1].statement = ReturnStmt{ExprId{size - 1}};
    auto checked = VerifyTreeStructure(tree);
    Check(std::holds_alternative<TreeStructure>(checked));
    const auto report = std::get<TreeStructure>(checked);
    Check(report.maximum_depth == size + 2);
    analysis::SemanticBudget budget;
    budget.work = report.work - 1;
    Check(std::get<TreeStructureError>(VerifyTreeStructure(tree, budget)) == TreeStructureError::kWorkLimit);
    budget.work = report.work;
    budget.bytes = report.allocated_bytes;
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree, budget)));
    budget.bytes -= 1;
    Check(std::get<TreeStructureError>(VerifyTreeStructure(tree, budget)) == TreeStructureError::kResourceLimit);
  }
}
}  // namespace
int main() {
  Ownership();
  ScopesAndBindings();
  OriginsAndAnchors();
  DeepTrees();
}
