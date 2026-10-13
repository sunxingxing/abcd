#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
// Pinned v7 dynamic ISA. Compact lexical slot one is the high nibble.
constexpr std::uint8_t kPush = 0x09, kLoad = 0x60, kStore = 0x3d,
    kPop = 0x69, kFunction = 0x33, kReturnUndefined = 0x65, kSlotOne = 0x10;
constexpr FunctionOwnerId kParent{0};
SemanticBudget TreeBudget() {
  SemanticBudget budget;
  // Several independent graph/tree verifiers run in each construction. This
  // bounded fixture allowance is not a production work-limit change.
  constexpr std::uint64_t kFixtureWork = kDefaultSemanticWork;
  budget.work = kFixtureWork;
  return budget;
}
struct Fixture {
  explicit Fixture(const std::vector<std::uint8_t>& code, bool capture) {
    const auto body = test::Body(code);
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
    RouteInputIdentity identity;
    identity.profile = abc::ProfileId::kDynamic1206;
    identity.abc_version = {12, 0, 6, 0};
    const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {200}, {100}, body,
        frame, InputCaptureMode::kConditional));
    auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
    std::vector<FunctionBinding> functions;
    for (std::size_t i = 0; i < values.graph.ops.size(); ++i) {
      const auto& op = values.graph.ops[i];
      if (!std::holds_alternative<ValueFunctionCreation>(op.operation)) continue;
      FunctionBinding function{{static_cast<std::uint32_t>(i)},
          {static_cast<std::uint32_t>(functions.size() + 1)}};
      if (capture) {
        const auto environment = std::get<EnvironmentId>(*values.environments->flow.before[i]);
        const auto allocation = values.cells->allocations[environment.value];
        function.captures.push_back(*FindCell(*values.cells, AllocatedCell{allocation, {0}}));
      }
      functions.push_back(std::move(function));
    }
    graph = std::move(values.graph);
    control = std::get<ControlPlan>(BuildControlPlan(graph, TreeBudget()));
    evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, TreeBudget(), IncomingCells::kReject, functions));
    constants.resize(graph.ops.size());
    FunctionSyntax syntax{};
    syntax.identity = identity; syntax.method = {200}; syntax.owner = kParent;
    syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.parameters.resize(frame.parameters);
    syntax.texts = {u"number"};
    tree = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation,
        std::move(syntax), {constants, 0, 1, TextId{0}}, TreeBudget())).tree;
    flow = std::get<TreeFlow>(VerifyTreeFlow(graph, control, evaluation, tree, TreeBudget()));
  }
  bool Valid(const FunctionTree& candidate) const {
    return std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation,
        candidate, {constants, 0, 1, TextId{0}}, TreeBudget()));
  }
  ValueProgram graph;
  ControlPlan control;
  EvaluationPlan evaluation;
  std::vector<ExpressionConstant> constants;
  FunctionTree tree;
  TreeFlow flow;
};

void Creations(std::uint32_t count) {
  std::vector<std::uint8_t> code;
  for (std::uint32_t i = 0; i < count; ++i)
    code.insert(code.end(), {kPush, 1, kLoad, 0, kStore, 0, kFunction, 0, 0, 0, 0, kPop});
  code.push_back(kReturnUndefined);
  Fixture fixture(code, true);
  test::Check(fixture.Valid(fixture.tree) && fixture.evaluation.functions.size() == count &&
      fixture.tree.closure_captures.size() == count);
  for (std::size_t i = 0; i < fixture.evaluation.functions.size(); ++i) {
    const auto& binding = fixture.evaluation.functions[i];
    const auto op = binding.creation.value;
    test::Check(fixture.evaluation.operations[op].kind == EvaluationKind::kRoot);
    const auto expression = *fixture.flow.expressions[op];
    const auto& node = std::get<FunctionExpr>(fixture.tree.expressions[expression.value].expression);
    test::Check(node.child == binding.child && node.captures.count == 1);
    test::Check(fixture.tree.closure_captures[node.captures.first] == *fixture.flow.cells[binding.captures[0].value]);
    test::Check(fixture.tree.expressions[expression.value].origin.value == fixture.graph.ops[op].origin.value);
  }
  if (count == 0) return;
  test::Check(std::holds_alternative<EvaluationFailure>(BuildEvaluationPlan(fixture.graph, TreeBudget())));
  test::Check(fixture.evaluation.cells->initialization.closure_writes == ClosureWrites::kLanguage);
  auto plan = fixture.evaluation; plan.functions.clear();
  test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(fixture.graph, plan, TreeBudget())));
  plan = fixture.evaluation; plan.cells->initialization.closure_writes = ClosureWrites::kUnknown;
  test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(fixture.graph, plan, TreeBudget())));
  const auto first = *fixture.flow.expressions[fixture.evaluation.functions[0].creation.value];
  auto changed = fixture.tree;
  std::get<FunctionExpr>(changed.expressions[first.value].expression).child = kParent;
  test::Check(!fixture.Valid(changed));
  changed = fixture.tree;
  std::get<FunctionExpr>(changed.expressions[first.value].expression).child = {count + 1};
  test::Check(!fixture.Valid(changed));
  changed = fixture.tree; changed.closure_captures[0] = changed.parameters[0].symbol;
  test::Check(!fixture.Valid(changed));
  changed = fixture.tree;
  std::get<FunctionExpr>(changed.expressions[first.value].expression).captures.count = 0;
  test::Check(!fixture.Valid(changed));
  if (count > 1) {
    const auto second = *fixture.flow.expressions[fixture.evaluation.functions[1].creation.value];
    changed = fixture.tree;
    std::get<FunctionExpr>(changed.expressions[second.value].expression).child = fixture.evaluation.functions[0].child;
    test::Check(!fixture.Valid(changed));
    changed = fixture.tree; changed.closure_captures[0] = changed.closure_captures[1];
    test::Check(!fixture.Valid(changed));
  }
}
void SharedValue() {
  // One freshly created function stored twice must stay one allocation.
  Fixture fixture({kPush, 2, kFunction, 0, 0, 0, 0, kStore, 0, kStore, kSlotOne, kPop, kReturnUndefined}, false);
  const auto operation = fixture.evaluation.functions[0].creation.value;
  test::Check(fixture.flow.snapshots[operation] &&
      fixture.evaluation.operations[operation].kind == EvaluationKind::kPredicateSnapshot);
  std::size_t count = 0;
  for (const auto& node : fixture.tree.expressions) count += std::holds_alternative<FunctionExpr>(node.expression);
  test::Check(count == 1 && fixture.Valid(fixture.tree));
  auto plan = fixture.evaluation;
  plan.operations[operation] = {EvaluationKind::kInline,
      fixture.graph.values[fixture.graph.ops[operation].result->value].first_use, {}};
  test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(fixture.graph, plan, TreeBudget())));
}
void CreationScopes() {
  constexpr std::uint8_t kJumpNonzero = 0x51;
  // Branch at PC 2 targets PC 17, after the first complete returning arm.
  constexpr std::uint8_t kOtherArmDelta = 15;
  Fixture branches({kLoad, 0, kJumpNonzero, kOtherArmDelta,
      kPush, 1, kLoad, 0, kStore, 0, kFunction, 0, 0, 0, 0, kPop, kReturnUndefined,
      kPush, 1, kLoad, 0, kStore, 0, kFunction, 0, 0, 0, 0, kPop, kReturnUndefined}, true);
  test::Check(branches.Valid(branches.tree) && branches.tree.closure_captures.size() == 2);
  const auto first = branches.tree.closure_captures[0], second = branches.tree.closure_captures[1];
  test::Check(branches.tree.symbols[first.value].scope != branches.tree.symbols[second.value].scope);
  auto changed = branches.tree;
  changed.closure_captures[0] = second;
  test::Check(std::get<TreeStructureError>(VerifyTreeStructure(changed, TreeBudget())) == TreeStructureError::kScope);
  // Branch at PC 14 returns to allocation PC 0. The source cell declaration
  // and creation must both remain inside the loop's per-iteration scope.
  constexpr std::int8_t kBackedgeDelta = -14;
  Fixture loop({kPush, 1, kLoad, 0, kStore, 0, kFunction, 0, 0, 0, 0,
      kPop, kLoad, 0, kJumpNonzero, static_cast<std::uint8_t>(kBackedgeDelta), kReturnUndefined}, true);
  test::Check(loop.Valid(loop.tree));
  const auto cell = loop.tree.closure_captures[0];
  const auto scope = loop.tree.symbols[cell.value].scope;
  test::Check(scope != loop.tree.body_scope && loop.tree.scopes[scope.value].kind == ScopeKind::kBlock);
}
}  // namespace
int main() {
  for (const auto count : {0U, 1U, 2U, 200U, 201U}) Creations(count);
  SharedValue();
  CreationScopes();
}
