#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "source/initialized_guards.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;

// Dynamic v7 ISA: newlexenv, lda, stlexvar, ldlexvar, poplexenv, return.
constexpr std::uint8_t kPush = 0x09, kLoad = 0x60, kStore = 0x3d,
    kRead = 0x3c, kPop = 0x69, kReturn = 0x64, kReturnUndefined = 0x65;

struct Fixture {
  explicit Fixture(std::vector<std::uint8_t> code) {
    const auto body = test::Body(code);
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
    graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
    test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
    control = std::get<ControlPlan>(BuildControlPlan(graph));
    evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    constants.resize(graph.ops.size());
    FunctionSyntax syntax{};
    syntax.owner = {0}; syntax.callable = CallableKind::kMethod;
    syntax.strictness = Strictness::kStrict;
    syntax.texts = {u"number"};
    syntax.parameters.resize(frame.parameters);
    auto constructed = std::get<ConstructedFunction>(BuildFunctionTree(
        graph, control, evaluation, std::move(syntax), {constants, 0, 1, TextId{0}}));
    original=constructed.flow->original;tree=std::move(constructed.tree);
  }
  bool Valid(const FunctionTree& candidate) const {
    return std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(
        graph, control, evaluation, candidate, {constants, 0, 1, TextId{0}}));
  }
  ValueProgram graph;
  ControlPlan control;
  EvaluationPlan evaluation;
  std::vector<ExpressionConstant> constants;
  FunctionTree tree;
  std::shared_ptr<const OriginalFlow> original;
};

void AllocationSites(std::uint32_t count) {
  std::vector<std::uint8_t> code;
  for (std::uint32_t i = 0; i < count; ++i)
    code.insert(code.end(), {kPush, 1, kLoad, 0, kStore, 0, kRead, 0, kPop});
  code.push_back(kReturnUndefined);
  Fixture fixture(std::move(code));
  test::Check(fixture.Valid(fixture.tree));
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(
      fixture.graph, fixture.control, fixture.evaluation, fixture.tree));
  test::Check(flow.cells.size() == count);
  std::size_t declarations = 0;
  for (std::size_t i = 0; i < fixture.tree.statements.size(); ++i) {
    const auto& statement = fixture.tree.statements[i];
    const auto* declaration = std::get_if<DeclarationStmt>(&statement.statement);
    if (!declaration || fixture.tree.symbols[declaration->symbol.value].kind != BindingKind::kLocal) continue;
    ++declarations;
    test::Check(!declaration->initializer &&
        std::holds_alternative<OriginalSite>(fixture.tree.origins[statement.origin.value]));
    if (count > 2) continue;
    auto changed = fixture.tree;
    changed.statements[i].origin = changed.statements[changed.body.value].origin;
    // The first allocation already owns the body origin; subsequent ones do not.
    if (statement.origin != changed.statements[i].origin) test::Check(!fixture.Valid(changed));
    changed = fixture.tree;
    changed.symbols[declaration->symbol.value].kind = BindingKind::kCompilerTemporary;
    changed.symbols[declaration->symbol.value].temporary = TemporaryReason::kEffectSnapshot;
    test::Check(!fixture.Valid(changed));
  }
  test::Check(declarations == count);
  if (count == 2) {
    for (std::size_t i = 0; i < fixture.tree.expressions.size(); ++i) {
      if (!std::holds_alternative<AssignmentExpr>(fixture.tree.expressions[i].expression)) continue;
      auto changed = fixture.tree;
      auto& assignment = std::get<AssignmentExpr>(changed.expressions[i].expression);
      auto* target = std::get_if<SymbolTarget>(&assignment.target);
      if (!target || target->symbol != *flow.cells[0]) continue;
      target->symbol = *flow.cells[1];
      test::Check(!fixture.Valid(changed));
      break;
    }
  }
}

void InitializedGuards() {
  for (const std::uint32_t count : {1U, 200U, 201U, 402U}) {
    std::vector<std::uint8_t> code{kPush, 1, kLoad, 0, kStore, 0};
    for (std::uint32_t i = 0; i < count; ++i)
      code.insert(code.end(), {kRead, 0, 0xfe, 9, 0, 0}); // throw.undefinedifhole
    code.push_back(kReturn);
    Fixture fixture(std::move(code));
    test::Check(fixture.Valid(fixture.tree));
    std::size_t guards = 0;
    for (std::size_t i = 0; i < fixture.graph.ops.size(); ++i) {
      if (!std::holds_alternative<ValueGuard>(fixture.graph.ops[i].operation)) continue;
      ++guards;
      test::Check(fixture.evaluation.operations[i].kind == EvaluationKind::kCellGuard);
      test::Check(IsInitializedCellGuard(fixture.graph, *fixture.evaluation.cells, OpId{static_cast<std::uint32_t>(i)}));
      auto forged = fixture.evaluation;
      forged.operations[i].kind = EvaluationKind::kRoot;
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(fixture.graph, forged)));
      if (guards == 1) {
        const auto& use = fixture.graph.uses[fixture.graph.ops[i].first_use.value];
        const auto load = std::get<OpResult>(fixture.graph.values[std::get<ValueId>(use.operand).value].origin).op;
        if (count == 1) test::Check(fixture.evaluation.operations[load.value].kind == EvaluationKind::kInline);
        auto uninitialized = fixture.graph;
        uninitialized.values[std::get<ValueId>(use.operand).value].domain = ValueKinds::Hole();
        test::Check(!IsInitializedCellGuard(uninitialized, *fixture.evaluation.cells,
            OpId{static_cast<std::uint32_t>(i)}));
        test::Check(std::holds_alternative<EvaluationFailure>(BuildEvaluationPlan(uninitialized)));
        forged = fixture.evaluation;
        forged.cells->initialization.reads[load.value] = NotCellRead{};
        test::Check(!IsInitializedCellGuard(fixture.graph, *forged.cells, OpId{static_cast<std::uint32_t>(i)}));
        test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(fixture.graph, forged)));
        forged=fixture.evaluation;
        forged.cells->identities.canonical[std::get<ValueId>(use.operand).value]=ValueId{static_cast<unsigned>(fixture.graph.values.size())};
        test::Check(std::get<CellEvaluationError>(VerifyCellEvaluation(fixture.graph,*forged.cells))==CellEvaluationError::kInternalIdentity);
        test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(fixture.graph,forged)));
      }
    }
    test::Check(guards == count);
    test::CheckInitializedGuards(fixture.tree,*fixture.original,guards);
  }
}

void MultipleSlots() {
  // Compact lexical address packs depth in the low nibble and slot in high.
  constexpr std::uint8_t kSlotOneDepthZero = 0x10;
  Fixture fixture({kPush, 2, kLoad, 0, kStore, 0, kStore, kSlotOneDepthZero,
      kRead, kSlotOneDepthZero, kPop, kReturn});
  test::Check(fixture.Valid(fixture.tree));
  const auto& body = std::get<BlockStmt>(fixture.tree.statements[fixture.tree.body.value].statement);
  std::optional<TreeOriginId> allocation;
  std::size_t cells = 0;
  for (std::uint32_t i = 0; i < body.child_count; ++i) {
    const auto& statement = fixture.tree.statements[fixture.tree.children[body.first_child + i].value];
    const auto* declaration = std::get_if<DeclarationStmt>(&statement.statement);
    if (!declaration || fixture.tree.symbols[declaration->symbol.value].kind != BindingKind::kLocal) continue;
    test::Check(!allocation || allocation == statement.origin);
    allocation = statement.origin; ++cells;
  }
  test::Check(cells == 2);
}

void IterationScope() {
  constexpr std::uint8_t kJumpNonzero = 0x51;
  constexpr std::int8_t kBackedgeDelta = -11;
  Fixture fixture({kPush, 1, kLoad, 0, kStore, 0, kRead, 0, kPop, kLoad, 0,
      kJumpNonzero, static_cast<std::uint8_t>(kBackedgeDelta), kReturnUndefined});
  test::Check(fixture.Valid(fixture.tree));
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(
      fixture.graph, fixture.control, fixture.evaluation, fixture.tree));
  test::Check(flow.cells.size() == 1 && flow.cells[0]);
  const auto symbol = *flow.cells[0];
  test::Check(fixture.tree.symbols[symbol.value].scope != fixture.tree.body_scope);
  auto changed = fixture.tree;
  changed.symbols[symbol.value].scope = changed.body_scope;
  test::Check(!fixture.Valid(changed));
  changed = fixture.tree;
  for (auto& anchor : changed.anchors) {
    if (anchor.origin.value != fixture.graph.ops[0].origin.value) continue;
    auto& boundary = std::get<BlockBoundary>(anchor.position);
    ++boundary.index;
    test::Check(!fixture.Valid(changed));
    break;
  }
}
}  // namespace
int main() {
  for (const auto count : {0U, 1U, 2U, 200U, 201U}) AllocationSites(count);
  InitializedGuards();
  MultipleSlots();
  IterationScope();
}
