#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_structure.h"
#include "../../../src/source/tree_semantics.h"
#include "analysis/ssa_fixture.h"

#include <string>

namespace {
using namespace abcd;
using namespace abcd::source;
constexpr FunctionOwnerId kFunction{2};
constexpr ModuleOwnerId kModule{7};
void Run(std::uint32_t count, bool captured) {
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  // Pinned dynamic v7 input forms. Modules below are conditional SOURCE aliases,
  // not fabricated module-read input proofs or permission to print this tree.
  const auto load = captured ? abc::Opcode::kLDLEXVAR_IMM4_IMM4 : abc::Opcode::kLDA_V8;
  constexpr std::uint8_t kFirstSlot = 0;
  const auto body = test::Body({static_cast<std::uint8_t>(load), kFirstSlot,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  RouteInputIdentity identity;
  identity.profile = abc::ProfileId::kDynamic1206;
  identity.abc_version = {12, 0, 6, 0};
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame,
      captured ? InputCaptureMode::kConditional : InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, {},
      captured ? IncomingCells::kConditional : IncomingCells::kReject));
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = {300}; syntax.owner = kFunction;
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.parameters.resize(frame.parameters);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto ascii = i == 0 ? std::string("$arg0") : "import" + std::to_string(i);
    syntax.names.emplace_back(ascii.begin(), ascii.end());
    syntax.modules.push_back({{kModule, {i}}, {i}, ModuleAccess::kReadOnly, NameEvidence::kMetadata});
  }
  if (captured) {
    syntax.names.push_back(u"captured");
    syntax.captures.push_back({{0}, {{1}, {0}}, {count}, NameEvidence::kMetadata});
  }
  std::vector<ExpressionConstant> operations(graph.ops.size());
  const ExpressionConstants constants{operations, syntax.names.size(), 0};
  const auto built = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation, syntax, constants));
  const auto& tree = built.tree;
  test::Check(tree.modules.size() == count && tree.module_scope.has_value() == (count != 0));
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, constants)));
  for (const auto& statement : tree.statements)
    if (const auto* declaration = std::get_if<DeclarationStmt>(&statement.statement))
      test::Check(tree.symbols[declaration->symbol.value].kind != BindingKind::kModule);
  if (count == 0) return;
  test::Check(tree.names[tree.symbols[tree.parameters[0].symbol.value].name.value] != u"$arg0");
  test::Check(tree.scopes[tree.parameter_scope.value].parent ==
      (tree.capture_scope ? tree.capture_scope : tree.module_scope));
  if (captured) test::Check(tree.scopes[tree.capture_scope->value].parent == tree.module_scope);
  const auto checked = std::get<TreeStructure>(VerifyTreeStructure(tree));
  analysis::SemanticBudget budget{checked.allocated_bytes, checked.work};
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree, budget)));
  --budget.work;
  test::Check(std::get<TreeStructureError>(VerifyTreeStructure(tree, budget)) == TreeStructureError::kWorkLimit);
  const auto reject = [&](const FunctionTree& changed) {
    test::Check(std::holds_alternative<TreeStructureError>(VerifyTreeStructure(changed)));
  };
  auto changed = tree; changed.module_scope.reset(); reject(changed);
  changed = tree; changed.modules.clear(); reject(changed);
  changed = tree; changed.symbols[changed.modules[0].symbol.value].scope = changed.body_scope; reject(changed);
  changed = tree; changed.modules[0].access = ModuleAccess::kMutable; reject(changed);
  if (count > 1) {
    changed = tree; changed.modules[1].target = changed.modules[0].target; reject(changed);
    changed = tree; ++changed.modules[1].target.owner.value; reject(changed);
  }
  // A lexical module reference is structurally legal, but cannot substitute for
  // an unrelated parameter/capture operand in the IR-to-tree proof.
  changed = tree;
  for (auto& expression : changed.expressions)
    if (std::holds_alternative<SymbolExpr>(expression.expression)) {
      expression.expression = SymbolExpr{changed.modules[0].symbol}; break;
    }
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(changed)));
  test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, changed, constants)));
  // Read-only imports cannot become assignment targets. Mutable local-module
  // aliases can, but this structural check still grants no IR correspondence.
  for (auto& expression : changed.expressions)
    if (std::holds_alternative<SymbolExpr>(expression.expression)) {
      const ExprId rhs{static_cast<std::uint32_t>(changed.expressions.size())};
      const auto origin = expression.origin;
      expression.expression = AssignmentExpr{SymbolTarget{changed.modules[0].symbol}, rhs};
      changed.expressions.push_back({TreeLiteral{UndefinedLiteral{}}, origin});
      break;
    }
  reject(changed);
  changed.modules[0].access = ModuleAccess::kMutable;
  changed.symbols[changed.modules[0].symbol.value].declaration = DeclarationKind::kLet;
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(changed)));
  if (!captured) {
    syntax.parameters[0] = {NameId{0}, NameEvidence::kMetadata};
    // Metadata parameters already reject the private '$' namespace before
    // constructing a tree. Keep that refusal, then exercise actual lexical
    // shadowing with an admitted identifier instead of extracting an error.
    test::Check(std::get<FunctionTreeError>(BuildFunctionTree(graph, control, evaluation, syntax, constants)) == FunctionTreeError::kName);
    syntax.names[0] = u"importedBinding";
    auto shadowed = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation, syntax, constants)).tree;
    for (auto& expression : shadowed.expressions)
      if (std::holds_alternative<SymbolExpr>(expression.expression)) {
        expression.expression = SymbolExpr{shadowed.modules[0].symbol}; break;
      }
    reject(shadowed);
  }
}
}  // namespace
int main() {
  for (auto count : {0U, 1U, 2U, 200U, 201U, 4096U})
    for (bool captured : {false, true}) Run(count, captured);
}
