#include "../../../src/source/tree_structure.h"
#include "../../../src/source/tree_labels.h"
#include "../../../src/source/family_print.h"
#include "abcd/source/tree_printer.h"

#include <cstdlib>
#include <bit>
#include <sstream>

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
void SuperContext() {
  for (const unsigned count : {0U, 1U, 200U, 201U, 402U}) {
    auto tree = Minimal(); tree.expressions.clear();
    for (unsigned i = 0; i < count; ++i) {
      tree.expressions.push_back({TreeLiteral{IntegerLiteral{static_cast<std::int32_t>(i)}}, {0}});
      tree.arguments.push_back({i});
    }
    tree.expressions.push_back({SuperCallExpr{{0, count}}, {0}});
    tree.statements[1].statement = ReturnStmt{ExprId{count}};
    for (const auto callable : {CallableKind::kFunction, CallableKind::kBaseConstructor, CallableKind::kMethod}) {
      tree.callable = callable; Expect(tree, TreeStructureError::kUnsupportedSyntax);
    }
    tree.callable = CallableKind::kDerivedConstructor;
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
    std::get<SuperCallExpr>(tree.expressions.back().expression).arguments.count++;
    Expect(tree, TreeStructureError::kRange);
  }
}
void YieldContext() {
  auto tree = Minimal();
  tree.expressions.push_back({YieldExpr{{0}}, {0}});
  tree.statements[1].statement = ReturnStmt{ExprId{1}};
  for (auto kind : {CallableKind::kFunction, CallableKind::kArrow, CallableKind::kAsyncFunction,
                    CallableKind::kAsyncArrow, CallableKind::kAsyncMethod}) {
    tree.callable = kind;
    Expect(tree, TreeStructureError::kUnsupportedSyntax);
    tree.expressions[1].expression = YieldExpr{{0}, YieldForm::kDelegate};
    Expect(tree, TreeStructureError::kUnsupportedSyntax);
    tree.expressions[1].expression = YieldExpr{{0}};
  }
  for (auto kind : {CallableKind::kGenerator, CallableKind::kGeneratorMethod}) {
    tree.callable = kind;
    tree.expressions[1].expression = YieldExpr{{0}};
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
    tree.expressions[1].expression = YieldExpr{{0}, YieldForm::kDelegate};
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
    tree.expressions[1].expression = YieldExpr{{0}, static_cast<YieldForm>(255)};
    Expect(tree, TreeStructureError::kUnsupportedSyntax);
    tree.expressions[1].expression = AwaitExpr{{0}};
    Expect(tree, TreeStructureError::kUnsupportedSyntax);
  }
  tree.callable = CallableKind::kAsyncGenerator;
  tree.expressions[1].expression = YieldExpr{{0}, YieldForm::kDirect};
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  tree.expressions[1].expression = AwaitExpr{{0}};
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
}
void InferredNames() {
  for (const auto callable : {CallableKind::kFunction, CallableKind::kArrow}) {
    for (const auto* name : {u"", u"__proto__", u"run", u"not an identifier"}) {
      auto tree = Minimal(); tree.callable = callable;
      tree.names.emplace_back(name); tree.inferred_name = NameId{0};
      Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
      // Syntax-only fixture: the runtime name is not a local symbol and does
      // not shadow any free read, even when its spelling matches that read.
      tree.names.emplace_back(u"run");
      tree.expressions[0].expression = GlobalNameExpr{{1}};
      Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
      const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
      std::ostringstream output;
      const auto printed = std::get<PrintedFamilyDraft>(PrintFamilyDraft(family, output));
      Check(output.str().starts_with("({[\"") && output.str().ends_with("\"]\n"));
      Check(output.str().substr(printed.members[0].expressions[0].begin,
          printed.members[0].expressions[0].end - printed.members[0].expressions[0].begin) == "run");
      Check(!tree.self && tree.symbols.empty());
      auto observable_allocation = family;
      observable_allocation.members[0].tree.identity.contract = SourceContract::kLanguageAndHostV1;
      std::ostringstream denied;
      Check(std::get<TreePrintError>(PrintFamilyDraft(observable_allocation, denied)) == TreePrintError::kUnsupportedContext);
      Check(denied.str().empty());
    }
  }
  auto tree = Minimal(); tree.inferred_name = NameId{0};
  Expect(tree, TreeStructureError::kName);
  tree.names.emplace_back(u"run"); tree.callable = CallableKind::kMethod;
  Expect(tree, TreeStructureError::kUnsupportedSyntax);
  tree.callable = CallableKind::kGeneratorMethod;
  Expect(tree, TreeStructureError::kUnsupportedSyntax);
  tree.callable = CallableKind::kFunction; tree.self = SymbolId{0};
  Expect(tree, TreeStructureError::kBinding); // Self still requires an actual immutable symbol/scope.
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
  const auto structure = std::get<TreeStructure>(VerifyTreeStructure(tree));
  Check(structure.expression_scopes == std::vector<ScopeId>{{2}});
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
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  tree.parameters[0].initializer = ExprId{0};
  Expect(tree, TreeStructureError::kUnsupportedSyntax);
  tree.parameters[0].initializer.reset();
  tree.names.push_back(u"later");
  tree.symbols.push_back({{0}, {1}, BindingKind::kParameter,
      DeclarationKind::kParameter, NameEvidence::kMetadata, {}});
  tree.parameters.push_back({{1}, {}, false});
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
void LiteralDefaults() {
  for (const TreeLiteral literal : {TreeLiteral{IntegerLiteral{-1}}, TreeLiteral{UndefinedLiteral{}},
                                   TreeLiteral{NumberLiteral{std::bit_cast<std::uint64_t>(-0.0)}}}) {
    auto tree = Minimal();
    tree.names.push_back(u"parameter");
    tree.symbols.push_back({{0}, {0}, BindingKind::kParameter,
        DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
    tree.parameters.push_back({{0}, ExprId{1}, false});
    tree.expressions.push_back({literal, {0}});
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
    tree.parameters[0].initializer = ExprId{0};
    tree.expressions.pop_back();
    Expect(tree, TreeStructureError::kOwnership); // Cannot share a body expression.
    tree.expressions.push_back({SymbolExpr{{0}}, {0}});
    tree.parameters[0].initializer = ExprId{1};
    Expect(tree, TreeStructureError::kUnsupportedSyntax); // Requires ordered parameter TDZ proof.
    tree.expressions[1].expression = ThisExpr{tree.owner};
    Expect(tree, TreeStructureError::kUnsupportedSyntax);
    tree.parameters[0].initializer = ExprId{2};
    Expect(tree, TreeStructureError::kRange);
  }
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
void LoopTargets() {
  auto tree = Minimal();
  tree.expressions.clear();
  tree.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
  tree.statements[1].statement = LoopStmt{{2}};
  tree.statements.push_back({BlockStmt{{2}, 1, 1}, {0}});
  tree.statements.push_back({ContinueStmt{{1}}, {0}});
  tree.children.push_back({3});
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  tree.statements[3].statement = BreakStmt{{1}};
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  tree.statements[3].statement = BreakStmt{{2}};
  Expect(tree, TreeStructureError::kTarget);
  tree.statements[3].statement = ContinueStmt{{99}};
  Expect(tree, TreeStructureError::kTarget);
  // A nested loop cannot silently turn an outer transfer into an unlabelled
  // transfer to the inner loop merely because both are syntactically valid.
  tree.scopes.push_back({ScopeKind::kBlock, ScopeId{2}});
  tree.statements[3].statement = LoopStmt{{4}};
  tree.statements.push_back({BlockStmt{{3}, 2, 1}, {0}});
  tree.statements.push_back({ContinueStmt{{1}}, {0}});
  tree.children.push_back({5});
  Expect(tree, TreeStructureError::kTarget);
  Check(std::holds_alternative<TreeStorage>(AssignTreeLabels(tree)));
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  Check(tree.labels.size() == 1 && tree.labels[0] == StmtId{1});
  const auto labelled = tree;
  std::get<ContinueStmt>(tree.statements[5].statement).label.reset();
  Expect(tree, TreeStructureError::kTarget);
  tree = labelled;
  std::get<ContinueStmt>(tree.statements[5].statement).label = LabelId{1};
  Expect(tree, TreeStructureError::kTarget);
  tree = labelled; tree.labels[0] = {3};
  Expect(tree, TreeStructureError::kTarget);
  tree = labelled; tree.statements[5].statement = BreakStmt{{1}, LabelId{0}};
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  tree.statements[5].statement = ContinueStmt{{3}};
  // Reassignment discards labels no longer required after a syntax rewrite.
  Check(std::holds_alternative<TreeStorage>(AssignTreeLabels(tree)));
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  tree = Minimal();
  tree.expressions.clear();
  tree.statements[1].statement = BreakStmt{{0}};
  Expect(tree, TreeStructureError::kTarget);
}
void DeepLoops() {
  for (const std::uint32_t depth : {200U, 201U, 4096U}) {
    auto tree = Minimal();
    tree.expressions.clear();
    tree.statements.resize(1);
    tree.children.clear();
    for (std::uint32_t i = 0; i < depth; ++i) {
      const auto loop = static_cast<std::uint32_t>(tree.statements.size());
      const auto scope = static_cast<std::uint32_t>(tree.scopes.size());
      tree.scopes.push_back({ScopeKind::kBlock, ScopeId{scope - 1}});
      tree.statements.push_back({LoopStmt{{loop + 1}}, {0}});
      tree.statements.push_back({BlockStmt{{scope}, i + 1, 1}, {0}});
      tree.children.push_back({loop});
    }
    const auto leaf = static_cast<std::uint32_t>(tree.statements.size());
    tree.statements.push_back({ContinueStmt{{leaf - 2}}, {0}});
    tree.children.push_back({leaf});
    const auto result = VerifyTreeStructure(tree);
    const auto* checked = std::get_if<TreeStructure>(&result);
    Check(checked && checked->maximum_depth == 2 * depth + 2);
    analysis::SemanticBudget budget;
    budget.work = checked->work;
    budget.bytes = checked->allocated_bytes;
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree, budget)));
    --budget.work;
    Check(std::get<TreeStructureError>(VerifyTreeStructure(tree, budget)) == TreeStructureError::kWorkLimit);
    tree.statements.back().statement = ContinueStmt{{1}};
    Expect(tree, TreeStructureError::kTarget);
    Check(std::holds_alternative<TreeStorage>(AssignTreeLabels(tree)));
    Check(tree.labels.size() == 1 && tree.labels[0] == StmtId{1});
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  }
}
void BlockExits() {
  for (const std::uint32_t depth : {1U, 200U, 201U, 402U}) {
    auto tree = Minimal(); tree.expressions.clear(); tree.statements.resize(1); tree.children.clear();
    tree.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
    tree.statements.push_back({BlockStmt{{2}, 1, 1}, {0}}); tree.children.push_back({1});
    for (std::uint32_t i = 0; i < depth; ++i) {
      const StmtId loop{static_cast<std::uint32_t>(tree.statements.size())};
      const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};
      tree.children.push_back(loop);
      tree.scopes.push_back({ScopeKind::kBlock, ScopeId{scope.value - 1}});
      tree.statements.push_back({LoopStmt{{loop.value + 1}}, {0}});
      tree.statements.push_back({BlockStmt{scope, static_cast<std::uint32_t>(tree.children.size()), 1}, {0}});
    }
    const StmtId exit{static_cast<std::uint32_t>(tree.statements.size())};
    tree.statements.push_back({BreakStmt{{1}}, {0}}); tree.children.push_back(exit);
    Check(std::holds_alternative<TreeStorage>(AssignTreeLabels(tree)));
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
    Check(tree.labels == std::vector<StmtId>{{1}});
    std::ostringstream output;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
    Check(output.str().find("$exit0: {") != std::string::npos && output.str().find("break $exit0;") != std::string::npos);
    auto invalid = tree;
    invalid.statements[exit.value].statement = ContinueStmt{{1}, LabelId{0}};
    Expect(invalid, TreeStructureError::kTarget);
    Check(std::get<TreeStructureError>(AssignTreeLabels(invalid)) == TreeStructureError::kTarget);
    invalid = tree; std::get<BreakStmt>(invalid.statements[exit.value].statement).label.reset();
    Expect(invalid, TreeStructureError::kTarget);
    invalid = tree; invalid.labels[0] = {0}; Expect(invalid, TreeStructureError::kTarget);
    invalid = tree; std::get<BlockStmt>(invalid.statements[0].statement).label = LabelId{0};
    Expect(invalid, TreeStructureError::kScope);
  }
}
void RequiredTryBlocks() {
  // A label belongs to a Statement, while try/catch/finally require a Block
  // directly. Valid label targets alone must not authorize invalid grammar.
  for (const StmtId target : {StmtId{2}, StmtId{3}}) {
    auto tree = Minimal(); tree.expressions.clear();
    tree.scopes.push_back({ScopeKind::kBlock, tree.body_scope});
    tree.scopes.push_back({ScopeKind::kBlock, tree.body_scope});
    tree.statements = {{BlockStmt{tree.body_scope, 0, 1}, {0}},
        {TryStmt{{2}, {}, StmtId{3}}, {0}}, {BlockStmt{{2}, 1, 0}, {0}},
        {BlockStmt{{3}, 1, 0}, {0}}};
    tree.children = {{1}};
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
    const auto exit = StmtId{static_cast<std::uint32_t>(tree.statements.size())};
    tree.statements.push_back({BreakStmt{target}, {0}}); tree.children.push_back(exit);
    std::get<BlockStmt>(tree.statements[target.value].statement).child_count = 1;
    Check(std::holds_alternative<TreeStorage>(AssignTreeLabels(tree)));
    Expect(tree, TreeStructureError::kUnsupportedSyntax);
  }
}
void StaticUndefinedFields() {
  for (const unsigned count : {1U, 200U, 201U, 402U}) {
    auto tree = Minimal();
    tree.callable = CallableKind::kMethod;
    tree.expressions.clear(); tree.statements.resize(1); tree.children.clear();
    for (unsigned i = 0; i < count; ++i) {
      const auto first = static_cast<std::uint32_t>(tree.expressions.size());
      const NameId name{static_cast<std::uint32_t>(tree.names.size())};
      const auto generated = "field" + std::to_string(i);
      tree.names.push_back(i == 0 ? u"constructor" : i == 1 ? u"prototype" :
          i == 2 ? u"field name" : std::u16string(generated.begin(), generated.end()));
      tree.expressions.push_back({ThisExpr{tree.owner}, {0}});
      tree.expressions.push_back({TreeLiteral{UndefinedLiteral{}}, {0}});
      tree.expressions.push_back({FieldDefinitionExpr{{first}, name, {first + 1}}, {0}});
      tree.children.push_back({static_cast<std::uint32_t>(tree.statements.size())});
      tree.statements.push_back({ExpressionStmt{{first + 2}}, {0}});
    }
    const ExprId completion{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({TreeLiteral{UndefinedLiteral{}}, {0}});
    tree.children.push_back({static_cast<std::uint32_t>(tree.statements.size())});
    tree.statements.push_back({ReturnStmt{completion}, {0}});
    tree.statements[0].statement = BlockStmt{tree.body_scope, 0, count + 1};
    std::ostringstream output;
    const auto result = PrintStaticFields(tree, output);
    Check(std::holds_alternative<PrintedFunction>(result));
    const auto& printed = std::get<PrintedFunction>(result);
    Check(output.str().find(" = ") == std::string::npos);
    Check(output.str().find("static [\"constructor\"];") != std::string::npos);
    if (count > 1) Check(output.str().find("static [\"prototype\"];") != std::string::npos);
    if (count > 2) Check(output.str().find("static [\"field name\"];") != std::string::npos);
    for (unsigned i = 0; i < count; ++i) {
      const auto& omitted = printed.expressions[i * 3 + 1];
      const auto& field = printed.expressions[i * 3 + 2];
      Check(omitted.begin == omitted.end && omitted.begin > 0);
      Check(omitted.begin >= field.begin && omitted.end <= field.end);
    }
    // Other expressions still execute. Syntax shortening is only for the
    // undefined literal, never a read/conversion or an arbitrary RHS producer.
    const ExprId operand{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({TreeLiteral{UndefinedLiteral{}}, {0}});
    tree.expressions[1].expression = UnaryExpr{TreeUnary::kTypeof, operand};
    std::ostringstream evaluated;
    Check(std::holds_alternative<PrintedFunction>(PrintStaticFields(tree, evaluated)));
    Check(evaluated.str().find(" = ") != std::string::npos);
  }
}
}  // namespace
int main() {
  SuperContext();
  YieldContext();
  InferredNames();
  Ownership();
  ScopesAndBindings();
  OriginsAndAnchors();
  LiteralDefaults();
  DeepTrees();
  LoopTargets();
  DeepLoops();
  BlockExits();
  RequiredTryBlocks();
  StaticUndefinedFields();
}
