#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"
#include "../../../src/source/function_abi.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
// Pinned v7 dynamic instructions; wide lexical operands are depth/slot u16.
constexpr std::uint8_t kPush = 0x09, kLoad = 0x60, kSave = 0x61,
    kStore = 0x3d, kRead = 0x3c, kPop = 0x69, kReturn = 0x64,
    kReturnUndefined = 0x65, kWide = 0xfd, kWideRead = 0x0c, kWideStore = 0x0d;
constexpr FunctionOwnerId kParent{1}, kChild{2};
std::u16string Name(std::uint32_t index) {
  const auto digits = std::to_string(index);
  std::u16string name = u"capture";
  name.append(digits.begin(), digits.end());
  return name;
}
struct Fixture {
  Fixture(const std::vector<std::uint8_t>& code, CallableKind kind = CallableKind::kFunction) {
    const auto body = test::Body(code);
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 2, {false, false, false, false}));
    RouteInputIdentity identity;
    identity.profile = abc::ProfileId::kDynamic1206;
    identity.abc_version = {12, 0, 6, 0};
    const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100},
        body, frame, InputCaptureMode::kConditional));
    auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
    graph = std::move(values.graph);
    control = std::get<ControlPlan>(BuildControlPlan(graph));
    evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, {}, IncomingCells::kConditional));
    constants.resize(graph.ops.size());
    syntax.identity = identity; syntax.method = {300}; syntax.owner = kChild;
    syntax.callable = kind; syntax.strictness = Strictness::kStrict;
    syntax.parameters.resize(frame.parameters);
    if (evaluation.cells) {
      const auto& cells = evaluation.cells->cells.cells;
      for (std::size_t i = 0; i < cells.size(); ++i) {
        if (!std::holds_alternative<IncomingCell>(cells[i].storage)) continue;
        const auto ordinal = static_cast<std::uint32_t>(syntax.captures.size());
        syntax.names.push_back(Name(ordinal));
        syntax.captures.push_back({CellId{static_cast<std::uint32_t>(i)},
            {kParent, SymbolId{ordinal}}, NameId{ordinal}});
      }
    }
    tree = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation, syntax,
        Constants())).tree;
    const auto abi = std::get<ConstructedAbi>(BuildFunctionAbi(graph, tree, frame, frame.parameters));
    test::Check(abi.abi.parameters.size() == frame.parameters);
  }
  ExpressionConstants Constants() const { return {constants, syntax.names.size(), 0}; }
  bool Valid(const FunctionTree& candidate) const {
    return std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, candidate, Constants()));
  }
  ValueProgram graph;
  ControlPlan control;
  EvaluationPlan evaluation;
  std::vector<ExpressionConstant> constants;
  FunctionSyntax syntax;
  FunctionTree tree;
};

void Cells(std::uint8_t count, CallableKind kind) {
  std::vector<std::uint8_t> code{kPush, 1, kLoad, 0, kStore, 0, kPop};
  for (std::uint16_t i = 0; i < count; ++i) {
    const auto slot = static_cast<std::uint8_t>(i);
    code.insert(code.end(), {kWide, kWideRead, 0, 0, slot, 0,
        kWide, kWideStore, 0, 0, slot, 0});
  }
  code.push_back(kReturnUndefined);
  Fixture fixture(code, kind);
  const auto& tree = fixture.tree;
  test::Check(fixture.Valid(tree) && tree.captures.size() == count && tree.capture_scope.has_value() == (count != 0));
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(fixture.graph, fixture.control, fixture.evaluation, tree));
  test::Check(flow.cells.size() == count + 1U);
  std::size_t local_declarations = 0;
  for (const auto& statement : tree.statements) {
    const auto* declaration = std::get_if<DeclarationStmt>(&statement.statement);
    if (!declaration) continue;
    const auto binding = tree.symbols[declaration->symbol.value].kind;
    test::Check(binding != BindingKind::kCapture);
    local_declarations += binding == BindingKind::kLocal;
  }
  test::Check(local_declarations == 1);
  for (std::size_t i = 0; i < tree.captures.size(); ++i) {
    const auto& capture = tree.captures[i];
    test::Check(capture.target == CapturedSymbol{kParent, SymbolId{static_cast<std::uint32_t>(i)}});
    test::Check(flow.cells[i + 1] == capture.symbol && tree.symbols[capture.symbol.value].scope == tree.capture_scope);
  }
  if (count == 0) return;
  test::Check(std::holds_alternative<EvaluationError>(BuildEvaluationPlan(fixture.graph)));
  auto evaluation = fixture.evaluation;
  evaluation.cells->incoming = IncomingCells::kReject;
  test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(fixture.graph, evaluation)));
  auto missing = fixture.syntax; missing.captures.clear();
  test::Check(std::holds_alternative<FunctionTreeError>(BuildFunctionTree(fixture.graph, fixture.control,
      fixture.evaluation, std::move(missing), fixture.Constants())));
  auto changed = tree;
  changed.captures[0].target.owner = changed.owner;
  test::Check(!fixture.Valid(changed));
  changed = tree; changed.symbols[changed.captures[0].symbol.value].kind = BindingKind::kLocal;
  test::Check(!fixture.Valid(changed));
  changed = tree;
  changed.symbols[changed.captures[0].symbol.value].name = changed.symbols[changed.parameters[0].symbol.value].name;
  test::Check(!fixture.Valid(changed)); // A formal shadows the alleged outer reference.
  changed = tree; changed.captures[0].symbol = changed.parameters[0].symbol;
  test::Check(!fixture.Valid(changed));
  changed = tree; changed.captures.clear();
  test::Check(!fixture.Valid(changed));
  changed = tree;
  for (auto& statement : changed.statements) {
    auto* declaration = std::get_if<DeclarationStmt>(&statement.statement);
    if (declaration && changed.symbols[declaration->symbol.value].kind == BindingKind::kLocal) {
      declaration->symbol = changed.captures[0].symbol;
      break;
    }
  }
  test::Check(!fixture.Valid(changed));
}
void Snapshot() {
  // Keep the old incoming-cell value, overwrite the cell, return the old value.
  Fixture fixture({kRead, 0, kSave, 1, kLoad, 0, kStore, 0, kLoad, 1, kReturn});
  const auto& tree = fixture.tree;
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(fixture.graph, fixture.control, fixture.evaluation, tree));
  test::Check(tree.captures.size() == 1 && flow.snapshots[0] && flow.cells[0] && flow.snapshots[0] != flow.cells[0]);
  test::Check(tree.symbols[flow.snapshots[0]->value].kind == BindingKind::kCompilerTemporary &&
      tree.symbols[flow.cells[0]->value].kind == BindingKind::kCapture);
  auto changed = tree;
  for (auto& expression : changed.expressions) {
    auto* reference = std::get_if<SymbolExpr>(&expression.expression);
    if (reference && reference->symbol == *flow.snapshots[0]) {
      reference->symbol = *flow.cells[0];
      break;
    }
  }
  test::Check(!fixture.Valid(changed));
}
void NameCollisions(std::uint32_t reserved) {
  Fixture fixture({kPush, 1, kLoad, 0, kStore, 0, kPop, kRead, 0, kReturn});
  auto syntax = fixture.syntax;
  std::u16string local_name;
  for (const auto& symbol : fixture.tree.symbols)
    if (symbol.kind == BindingKind::kLocal) local_name = fixture.tree.names[symbol.name.value];
  test::Check(!local_name.empty());
  // Real parent-generated aliases may use the same namespace as a child's
  // native locals. Preserve the alias and rename only the child's declaration.
  syntax.names[syntax.captures[0].name.value] = local_name;
  for (std::uint32_t i = 0; i < reserved; ++i) {
    const auto digits = std::to_string(i);
    std::u16string name = u"$binding";
    name.append(digits.begin(), digits.end());
    syntax.names.push_back(std::move(name));
  }
  const ExpressionConstants constants{fixture.constants, syntax.names.size(), 0};
  const auto build = [&](const FunctionSyntax& input) {
    return std::get<ConstructedFunction>(BuildFunctionTree(fixture.graph, fixture.control,
        fixture.evaluation, input, constants)).tree;
  };
  const auto tree = build(syntax);
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(fixture.graph,
      fixture.control, fixture.evaluation, tree, constants)));
  test::Check(tree.names[tree.symbols[tree.captures[0].symbol.value].name.value] == local_name);
  for (const auto& symbol : tree.symbols)
    if (symbol.kind == BindingKind::kLocal) test::Check(tree.names[symbol.name.value] != local_name);
  // Synthesized formals obey the same allocator, not a constructor-only path.
  syntax.names[syntax.captures[0].name.value] = u"$arg0";
  const auto parameters = build(syntax);
  test::Check(parameters.names[parameters.symbols[parameters.parameters[0].symbol.value].name.value] != u"$arg0");
  const auto repeated = build(syntax);
  test::Check(parameters.names == repeated.names);
}
}  // namespace
int main() {
  for (const auto count : {0U, 1U, 2U, 200U, 201U}) Cells(static_cast<std::uint8_t>(count), CallableKind::kFunction);
  Cells(2, CallableKind::kArrow);
  Snapshot();
  for (const auto count : {0U, 1U, 2U, 200U, 201U}) NameCollisions(count);
}
