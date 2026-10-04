#include "../../../src/source/phi_copies.h"
#include "../../../src/source/binding_plan.h"
#include "../../../src/source/expression_forest.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_checks.h"

#include <bit>
#include <cstdlib>

namespace {
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
SemanticBudget PhiBudget(const ValueProgram& graph) {
  // Explicit O(U log U) fixture grant; production defaults are unchanged.
  constexpr std::uint64_t kWorkPerInputLogBound = 512;
  SemanticBudget budget;
  budget.work = kWorkPerInputLogBound * graph.uses.size() * std::bit_width(graph.uses.size());
  return budget;
}
void Append(ValueProgram& graph, OperandUse use) {
  const UseId id{static_cast<std::uint32_t>(graph.uses.size())};
  auto& definition = graph.values[std::get<ValueId>(use.operand).value];
  use.next = definition.first_use;
  if (use.next) graph.uses[use.next->value].previous = id;
  definition.first_use = id;
  graph.uses.push_back(use);
}
ValueProgram Graph(std::uint32_t count, bool loop) {
  ValueProgram graph;
  graph.blocks = {{{0}, 1, {0}, 2}, {{1}, 1, {2}, 0}};
  graph.edges = {{{0}, {loop ? 0U : 1U}, FlowEdgeKind::kTaken, 0},
                 {{0}, {1}, FlowEdgeKind::kFallthrough, 1}};
  graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  for (std::uint32_t i = 0; i < count; ++i)
    graph.values.push_back({PhiDefinition{{loop ? 0U : 1U}}, ValueKinds::Language(), {}});
  graph.ops.push_back({ValueBranch{UnaryOperator::kToBoolean}, {0}, 0, {0}, 1, {},
      LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {0}});
  graph.ops.push_back({ValueReturn{}, {1}, 0, {1}, 1, {},
      LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {1}});
  Append(graph, {OpId{0}, OperandRole::kCondition, ReadPurpose::kOperand, ValueId{0}, {}, {}, {}, {}});
  Append(graph, {OpId{1}, OperandRole::kReturnValue, ReadPurpose::kOperand, ValueId{1}, {}, {}, {}, {}});
  for (std::uint32_t i = 1; i <= count; ++i) {
    const IncomingKey first = loop ? IncomingKey{MethodEntryKey{}} : IncomingKey{EdgeId{0}};
    const IncomingKey second = EdgeId{loop ? 0U : 1U};
    Append(graph, {ValueId{i}, OperandRole::kPhiInput, ReadPurpose::kOperand, ValueId{0}, first, {}, {}, {}});
    Append(graph, {ValueId{i}, OperandRole::kPhiInput, ReadPurpose::kOperand,
        ValueId{loop ? i % count + 1 : 0}, second, {}, {}, {}});
  }
  return graph;
}
PhiCopies Plan(const ValueProgram& graph, SemanticBudget budget = {}) {
  auto built = BuildPhiCopies(graph, budget);
  Check(std::holds_alternative<PhiCopies>(built));
  auto plan = std::get<PhiCopies>(std::move(built));
  Check(std::holds_alternative<std::uint64_t>(VerifyPhiCopies(graph, plan, budget)));
  return plan;
}
FunctionSyntax Syntax() {
  FunctionSyntax syntax{};
  syntax.callable = CallableKind::kMethod;
  syntax.strictness = Strictness::kStrict;
  syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
  syntax.names = {u"input", u"field"};
  return syntax;
}
FunctionTree Tree(const ValueProgram& graph, const EvaluationPlan& evaluation,
    ExpressionConstants constants, SemanticBudget budget = {}) {
  const auto control = BuildControlPlan(graph, budget);
  Check(std::holds_alternative<ControlPlan>(control));
  auto built = BuildFunctionTree(graph, std::get<ControlPlan>(control), evaluation,
      Syntax(), constants, budget);
  Check(std::holds_alternative<ConstructedFunction>(built));
  auto tree = std::get<ConstructedFunction>(std::move(built)).tree;
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,
      std::get<ControlPlan>(control), evaluation, tree, constants, budget)));
  return tree;
}
void Mutations() {
  auto graph = Graph(2, false);
  const auto plan = Plan(graph);
  Check(plan.bundles.size() == 2 && plan.copies.size() == 4);
  Check(std::get<EdgeId>(plan.bundles[0].incoming) == EdgeId{0});
  Check(std::get<EdgeId>(plan.bundles[1].incoming) == EdgeId{1});
  auto changed = plan;
  ++changed.revision.value;
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kRevision);
  changed = plan; ++changed.cfg_revision.value;
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kRevision);
  changed = plan; changed.inputs[1] = changed.inputs[0];
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kInput);
  changed = plan; changed.copies[0].source = ValueId{1};
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kInput);
  changed = plan; changed.bundles[0].incoming = MethodEntryKey{};
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kInput);
  changed = plan; changed.bundles.pop_back();
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kBundle);
  changed = plan; changed.bundles[0].schedule.steps.clear();
  Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, changed)) == PhiCopyError::kSchedule);
  graph.values[1].domain = ValueKinds::Hole();
  Check(std::get<PhiCopyError>(BuildPhiCopies(graph)) == PhiCopyError::kDomain);
  graph = Graph(2, false); graph.uses[3].incoming = EdgeId{0};
  Check(std::get<PhiCopyError>(BuildPhiCopies(graph)) == PhiCopyError::kFlow);
  graph = Graph(2, false);
  SemanticBudget budget;
  budget.work = plan.work;
  Check(std::holds_alternative<PhiCopies>(BuildPhiCopies(graph, budget)));
  --budget.work;
  Check(std::get<PhiCopyError>(BuildPhiCopies(graph, budget)) == PhiCopyError::kWorkLimit);
  budget = {}; budget.bytes = 0;
  Check(std::get<PhiCopyError>(BuildPhiCopies(graph, budget)) == PhiCopyError::kResourceLimit);
}
void EntryAndScaling() {
  for (const std::uint32_t count : {1U, 2U, 200U, 201U, 4096U}) {
    for (const bool loop : {false, true}) {
      const auto graph = Graph(count, loop);
      auto budget = PhiBudget(graph);
      const auto plan = Plan(graph, budget);
      Check(plan.bundles.size() == 2);
      Check(plan.bundles[0].count == count && plan.bundles[1].count == count);
      Check(std::holds_alternative<MethodEntryKey>(plan.bundles[0].incoming) == loop);
      Check(plan.bundles[0].schedule.temporaries == 0);
      Check(plan.bundles[1].schedule.temporaries == (loop && count > 1 ? 1U : 0U));
      const auto evaluated = BuildEvaluationPlan(graph, budget);
      Check(std::holds_alternative<EvaluationPlan>(evaluated));
      const auto& evaluation = std::get<EvaluationPlan>(evaluated);
      Check(evaluation.phis && evaluation.phis->bundles.size() == 2);
      const auto bound = BuildBindingPlan(graph, evaluation, 1, budget);
      Check(std::holds_alternative<BindingPlan>(bound));
      const auto& bindings = std::get<BindingPlan>(bound);
      for (std::uint32_t i = 1; i <= count; ++i) {
        const auto symbol = std::get<SymbolId>(bindings.values[i]);
        Check(std::get<PhiBinding>(bindings.symbols[symbol.value]).value == ValueId{i});
      }
      Check(bindings.cycle_bases.size() == 2 && !bindings.cycle_bases[0]);
      Check(bindings.cycle_bases[1].has_value() == (loop && count > 1));
      if (const auto base = bindings.cycle_bases[1]) {
        const auto cycle = std::get<CycleBinding>(bindings.symbols[base->value]);
        Check(cycle.bundle == 1 && cycle.temporary == CopyTempId{0});
      }
      auto missing = evaluation;
      missing.phis.reset();
      Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, missing, budget)) == EvaluationError::kLivePhi);
      Check(plan.work <= budget.work);
      budget.work = plan.work;
      Check(std::holds_alternative<PhiCopies>(BuildPhiCopies(graph, budget)));
      --budget.work;
      Check(std::get<PhiCopyError>(BuildPhiCopies(graph, budget)) == PhiCopyError::kWorkLimit);
    }
  }
}
void PhiInputSnapshot() {
  ValueProgram graph;
  graph.values = {{EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}},
                  {OpResult{{0}}, ValueKinds::Language(), {}},
                  {PhiDefinition{{1}}, ValueKinds::Language(), {}}};
  graph.blocks = {{{0}, 2, {0}, 2}, {{2}, 1, {2}, 0}};
  graph.edges = {{{0}, {1}, FlowEdgeKind::kTaken, 0}, {{0}, {1}, FlowEdgeKind::kFallthrough, 1}};
  const auto effects = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
  graph.ops = {
      {ValueNamedRead{{0}}, {0}, 0, {0}, 1, ValueId{1}, effects, effects, ExceptionalCompletion::kMayThrow, {0}},
      {ValueBranch{UnaryOperator::kToBoolean}, {0}, 1, {1}, 1, {}, LanguageEffect::kNone,
       LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {1}},
      {ValueReturn{}, {1}, 0, {2}, 1, {}, LanguageEffect::kNone,
       LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {2}}};
  Append(graph, {OpId{0}, OperandRole::kBase, ReadPurpose::kOperand, ValueId{0}, {}, {}, {}, {}});
  Append(graph, {OpId{1}, OperandRole::kCondition, ReadPurpose::kOperand, ValueId{0}, {}, {}, {}, {}});
  Append(graph, {OpId{2}, OperandRole::kReturnValue, ReadPurpose::kOperand, ValueId{2}, {}, {}, {}, {}});
  Append(graph, {ValueId{2}, OperandRole::kPhiInput, ReadPurpose::kOperand, ValueId{1}, EdgeId{0}, {}, {}, {}});
  Append(graph, {ValueId{2}, OperandRole::kPhiInput, ReadPurpose::kOperand, ValueId{0}, EdgeId{1}, {}, {}, {}});
  const auto built = BuildEvaluationPlan(graph);
  Check(std::holds_alternative<EvaluationPlan>(built));
  const auto& evaluation = std::get<EvaluationPlan>(built);
  Check(evaluation.operations[0].kind == EvaluationKind::kRoot);
  Check(evaluation.operations[0].temporary == TemporaryReason::kCrossRegionValue);
  const auto bound = BuildBindingPlan(graph, evaluation, 1);
  Check(std::holds_alternative<BindingPlan>(bound));
  const auto& bindings = std::get<BindingPlan>(bound);
  const auto snapshot = std::get<SymbolId>(bindings.values[1]);
  Check(std::get<SnapshotBinding>(bindings.symbols[snapshot.value]).producer == OpId{0});
  const auto phi = std::get<SymbolId>(bindings.values[2]);
  Check(phi != snapshot && std::get<PhiBinding>(bindings.symbols[phi.value]).value == ValueId{2});
  graph.origins = {{{0}, {0}, {}}, {{1}, {1}, {}}, {{2}, {2}, {}}};
  const std::vector<ExpressionConstant> constants{NameId{1}, std::monostate{}, std::monostate{}};
  const auto syntax = BuildExpressionForest(graph, evaluation, 1, {0}, {constants, 2});
  Check(std::holds_alternative<ExpressionForest>(syntax));
  const auto& forest = std::get<ExpressionForest>(syntax);
  const auto& copied = std::get<AssignmentExpr>(
      forest.expressions[forest.edge_assignments[forest.edges[0].first].value].expression);
  Check(std::get<SymbolTarget>(copied.target).symbol == phi);
  Check(std::get<SymbolExpr>(forest.expressions[copied.value.value].expression).symbol == snapshot);
  // The getter remains at its operation root, not duplicated on either edge.
  Check(std::holds_alternative<MemberExpr>(forest.expressions[forest.operations[0]->value].expression));
  const auto tree = Tree(graph, evaluation, {constants, 2});
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto checked = std::get<TreeFlow>(VerifyTreeFlow(graph, control, evaluation, tree));
  Check(checked.copies.size() == 2 && checked.phis[2] == phi);
  const auto root = checked.copies[0].expressions[0];
  const auto rhs = std::get<AssignmentExpr>(tree.expressions[root.value].expression).value;
  auto changed = tree;
  changed.expressions[rhs.value].expression = SymbolExpr{SymbolId{0}};
  Check(std::get<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation,
      changed, {constants, 2})) == TreeSemanticError::kOperand);
  changed = tree;
  std::get<EdgeOrigin>(changed.origins[changed.expressions[root.value].origin.value]).edge = {1};
  Check(std::get<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation,
      changed, {constants, 2})) == TreeSemanticError::kFlow);
  auto forged = evaluation;
  forged.operations[0] = {EvaluationKind::kInline, UseId{3}, {}};
  Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, forged)) == EvaluationError::kOwnership);
  forged = evaluation;
  forged.phis->bundles[0].schedule.steps.clear();
  Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, forged)) == EvaluationError::kLivePhi);
}
// A getter may cross an inert decision only when every normal path reaches
// the same original phi observation. This diamond is an algorithm fixture;
// production must not require its shape or replay the getter on both arms.
ValueProgram InertTail() {
  ValueProgram graph;
  graph.blocks = {{{0}, 1, {0}, 1}, {{1}, 1, {1}, 2}, {{2}, 1, {3}, 1},
                  {{3}, 1, {4}, 1}, {{4}, 1, {5}, 1}, {{5}, 1, {6}, 0}};
  graph.edges = {{{0}, {1}, FlowEdgeKind::kFallthrough, 0},
                 {{1}, {2}, FlowEdgeKind::kTaken, 0},
                 {{1}, {3}, FlowEdgeKind::kFallthrough, 1},
                 {{2}, {4}, FlowEdgeKind::kFallthrough, 0},
                 {{3}, {4}, FlowEdgeKind::kFallthrough, 0},
                 {{4}, {5}, FlowEdgeKind::kFallthrough, 0}};
  graph.values = {
      {EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}},
      {OpResult{{0}, {}}, ValueKinds::Language(), {}},
      {PhiDefinition{{5}}, ValueKinds::Language(), {}}};
  const auto effects = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
  graph.ops = {{ValueNamedRead{{0}},
                {0},
                0,
                {0},
                1,
                ValueId{1},
                effects,
                effects,
                ExceptionalCompletion::kMayThrow,
                {0}},
               {ValueBranch{UnaryOperator::kToBoolean},
                {1},
                0,
                {1},
                1,
                {},
                LanguageEffect::kNone,
                LanguageEffect::kNone,
                ExceptionalCompletion::kImpossible,
                {1}},
               {ValueJump{},
                {2},
                0,
                {2},
                0,
                {},
                LanguageEffect::kNone,
                LanguageEffect::kNone,
                ExceptionalCompletion::kImpossible,
                {2}},
               {ValueJump{},
                {3},
                0,
                {2},
                0,
                {},
                LanguageEffect::kNone,
                LanguageEffect::kNone,
                ExceptionalCompletion::kImpossible,
                {3}},
               {ValueJump{},
                {4},
                0,
                {2},
                0,
                {},
                LanguageEffect::kNone,
                LanguageEffect::kNone,
                ExceptionalCompletion::kImpossible,
                {4}},
               {ValueReturn{},
                {5},
                0,
                {2},
                1,
                {},
                LanguageEffect::kNone,
                LanguageEffect::kNone,
                ExceptionalCompletion::kImpossible,
                {5}}};
  Append(graph, {OpId{0},
                 OperandRole::kBase,
                 ReadPurpose::kOperand,
                 ValueId{0},
                 {},
                 {},
                 {},
                 {}});
  Append(graph, {OpId{1},
                 OperandRole::kCondition,
                 ReadPurpose::kOperand,
                 ValueId{0},
                 {},
                 {},
                 {},
                 {}});
  Append(graph, {OpId{5},
                 OperandRole::kReturnValue,
                 ReadPurpose::kOperand,
                 ValueId{2},
                 {},
                 {},
                 {},
                 {}});
  Append(graph, {ValueId{2},
                 OperandRole::kPhiInput,
                 ReadPurpose::kOperand,
                 ValueId{1},
                 EdgeId{5},
                 {},
                 {},
                 {}});
  return graph;
}
void PhiExpressionTails() {
  const auto candidate = [](const ValueProgram& graph) {
    const auto copies = Plan(graph);
    TreeCheckBudget budget({});
    return IndexPhiExpressionInputs(graph, copies, {}, {}, budget)[0];
  };
  const auto graph = InertTail();
  Check(candidate(graph) == UseId{3});
  auto changed = graph;
  changed.edges[4].target = {3};  // One successor diverges forever.
  Check(!candidate(changed));
  changed = graph;
  changed.ops[3].operation = ValueDebugger{};
  changed.ops[3].normal = LanguageEffect::kDebugger;
  Check(!candidate(changed));
  changed = graph;
  changed.values.push_back({PhiDefinition{{4}}, ValueKinds::Language(), {}});
  Append(changed, {ValueId{3},
                   OperandRole::kPhiInput,
                   ReadPurpose::kOperand,
                   ValueId{0},
                   EdgeId{3},
                   {},
                   {},
                   {}});
  Append(changed, {ValueId{3},
                   OperandRole::kPhiInput,
                   ReadPurpose::kOperand,
                   ValueId{0},
                   EdgeId{4},
                   {},
                   {},
                   {}});
  Check(!candidate(
      changed));  // An intervening parallel-copy event remains owned.
  changed = graph;
  changed.values[0].first_use = UseId{0};
  changed.uses[0].previous.reset();
  changed.uses[1].operand = ValueId{1};
  changed.uses[1].next = UseId{3};
  changed.uses[3].previous = UseId{1};
  changed.values[1].first_use = UseId{1};
  Check(!candidate(changed));  // A second observation must retain the snapshot.
  changed = graph;
  changed.edges[4].target = {6};  // One path returns without observing the phi.
  changed.blocks.push_back({{6}, 1, {6}, 0});
  changed.ops.push_back({ValueReturn{},
                         {6},
                         0,
                         {4},
                         1,
                         {},
                         LanguageEffect::kNone,
                         LanguageEffect::kNone,
                         ExceptionalCompletion::kImpossible,
                         {6}});
  Append(changed, {OpId{6},
                   OperandRole::kReturnValue,
                   ReadPurpose::kOperand,
                   ValueId{0},
                   {},
                   {},
                   {},
                   {}});
  Check(!candidate(changed));
  std::uint64_t previous_work = 0;
  for (const std::uint32_t count : {1U, 200U, 201U, 402U}) {
    changed = graph;
    changed.edges[0].target = {6};
    for (std::uint32_t i = 0; i < count; ++i) {
      const WorkBlockId block{6 + i};
      changed.blocks.push_back({OpId{6 + i}, 1, EdgeId{6 + i}, 1});
      changed.edges.push_back(
          {block, i + 1 == count ? WorkBlockId{1} : WorkBlockId{7 + i},
           FlowEdgeKind::kFallthrough, 0});
      changed.ops.push_back({ValueJump{},
                             block,
                             0,
                             {4},
                             0,
                             {},
                             LanguageEffect::kNone,
                             LanguageEffect::kNone,
                             ExceptionalCompletion::kImpossible,
                             {6 + i}});
    }
    const auto copies = Plan(changed);
    TreeCheckBudget budget({});
    Check(IndexPhiExpressionInputs(changed, copies, {}, {}, budget)[0] ==
          UseId{3});
    if (previous_work && count > 200) Check(budget.Work() <= previous_work * 3);
    previous_work = budget.Work();
  }
}
void EdgeSyntax() {
  for (const std::uint32_t count : {1U, 2U, 200U, 201U, 4096U}) {
    auto graph = Graph(count, false);
    graph.origins = {{{0}, {0}, {}}, {{1}, {1}, {}}};
    const auto allowance = PhiBudget(graph);
    const auto evaluated = BuildEvaluationPlan(graph, allowance);
    Check(std::holds_alternative<EvaluationPlan>(evaluated));
    const auto& evaluation = std::get<EvaluationPlan>(evaluated);
    const std::vector<ExpressionConstant> constants(graph.ops.size());
    const auto built = BuildExpressionForest(graph, evaluation, 1, {0}, {constants}, allowance);
    Check(std::holds_alternative<ExpressionForest>(built));
    const auto& forest = std::get<ExpressionForest>(built);
    Check(forest.edges.size() == 2 && forest.edge_assignments.size() == count * 2);
    std::vector<bool> owned(forest.expressions.size());
    for (std::uint32_t edge = 0; edge < 2; ++edge) {
      const auto& range = forest.edges[edge];
      Check(std::get<EdgeId>(range.incoming) == EdgeId{edge} && range.count == count && range.first == edge * count);
      const auto& schedule = evaluation.phis->bundles[edge].schedule;
      for (std::uint32_t i = 0; i < count; ++i) {
        const auto root = forest.edge_assignments[range.first + i];
        Check(!owned[root.value]); owned[root.value] = true;
        const auto& node = forest.expressions[root.value];
        Check(std::get<EdgeOrigin>(forest.origins[node.origin.value]).edge == EdgeId{edge});
        const auto& assignment = std::get<AssignmentExpr>(node.expression);
        const auto destination = std::get<ValueId>(schedule.steps[i].destination);
        Check(std::get<SymbolTarget>(assignment.target).symbol ==
              std::get<SymbolId>(forest.bindings.values[destination.value]));
        Check(!owned[assignment.value.value]); owned[assignment.value.value] = true;
        const auto& rhs = forest.expressions[assignment.value.value];
        Check(rhs.origin == node.origin);
        Check(std::get<SymbolExpr>(rhs.expression).symbol == SymbolId{0});
      }
    }
    SemanticBudget budget;
    budget.work = forest.work;
    Check(std::holds_alternative<ExpressionForest>(BuildExpressionForest(graph, evaluation, 1, {0}, {constants}, budget)));
    --budget.work;
    Check(std::get<ExpressionError>(BuildExpressionForest(graph, evaluation, 1, {0}, {constants}, budget)) == ExpressionError::kWorkLimit);
    const auto tree = Tree(graph, evaluation, {constants, 2}, allowance);
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    const auto flow = std::get<TreeFlow>(VerifyTreeFlow(graph, control, evaluation, tree, allowance));
    Check(flow.copies.size() == 2 && flow.copies[0].expressions.size() == count);
    for (std::uint32_t i = 1; i <= count; ++i) Check(flow.phis[i].has_value());
    if (count == 2) {
      auto changed = tree;
      const auto root = flow.copies[0].expressions[0];
      std::get<AssignmentExpr>(changed.expressions[root.value].expression).target = SymbolTarget{*flow.phis[2]};
      Check(std::get<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation,
          changed, {constants, 2})) == TreeSemanticError::kFlow);
      changed = tree;
      for (auto& statement : changed.statements) {
        if (auto* branch = std::get_if<IfStmt>(&statement.statement)) {
          Check(branch->alternate.has_value());
          std::swap(branch->consequent, *branch->alternate);
          break;
        }
      }
      Check(std::get<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation,
          changed, {constants, 2})) == TreeSemanticError::kFlow);
    }
  }
  auto entry = Graph(2, true);
  entry.origins = {{{0}, {0}, {}}, {{1}, {1}, {}}};
  const auto evaluated = BuildEvaluationPlan(entry);
  Check(std::holds_alternative<EvaluationPlan>(evaluated));
  const std::vector<ExpressionConstant> constants(entry.ops.size());
  Check(std::holds_alternative<ExpressionForest>(BuildExpressionForest(entry,
      std::get<EvaluationPlan>(evaluated), 1, {0}, {constants})));
  const auto entry_tree = Tree(entry, std::get<EvaluationPlan>(evaluated), {constants, 2});
  const auto control = std::get<ControlPlan>(BuildControlPlan(entry));
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(entry, control,
      std::get<EvaluationPlan>(evaluated), entry_tree));
  Check(std::holds_alternative<MethodEntryKey>(flow.copies[0].incoming));
  const auto& body = std::get<BlockStmt>(entry_tree.statements[entry_tree.body.value].statement);
  std::optional<std::uint32_t> loop_index;
  std::optional<std::uint32_t> entry_index;
  for (std::uint32_t i = 0; i < body.child_count; ++i) {
    const auto& statement = entry_tree.statements[entry_tree.children[body.first_child + i].value];
    if (std::holds_alternative<LoopStmt>(statement.statement)) loop_index = i;
    if (std::holds_alternative<EntryOrigin>(entry_tree.origins[statement.origin.value])) entry_index = i;
  }
  Check(loop_index && entry_index && *entry_index < *loop_index);
  auto changed = entry_tree;
  std::swap(changed.children[body.first_child + *entry_index], changed.children[body.first_child + *loop_index]);
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(entry, control,
      std::get<EvaluationPlan>(evaluated), changed, {constants, 2})));
  changed = entry_tree;
  const auto root = flow.copies[0].expressions[0];
  changed.origins[changed.expressions[root.value].origin.value] = EdgeOrigin{{0}};
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(entry, control,
      std::get<EvaluationPlan>(evaluated), changed, {constants, 2})));
}
void DiamondSyntax() {
  ValueProgram graph;
  graph.values = {{EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}},
                  {OpResult{{1}}, ValueKinds::Language(), {}},
                  {OpResult{{3}}, ValueKinds::Language(), {}},
                  {PhiDefinition{{3}}, ValueKinds::Language(), {}}};
  graph.blocks = {{{0}, 1, {0}, 2}, {{1}, 2, {2}, 1},
                  {{3}, 2, {3}, 1}, {{5}, 1, {4}, 0}};
  graph.edges = {{{0}, {1}, FlowEdgeKind::kTaken, 0},
                 {{0}, {2}, FlowEdgeKind::kFallthrough, 1},
                 {{1}, {3}, FlowEdgeKind::kTaken, 0},
                 {{2}, {3}, FlowEdgeKind::kTaken, 0}};
  const auto pure = LanguageEffect::kNone;
  const auto normal = ExceptionalCompletion::kImpossible;
  graph.ops = {
      {ValueBranch{UnaryOperator::kToBoolean}, {0}, 0, {0}, 1, {}, pure, pure, normal, {0}},
      {ValueCopy{}, {1}, 0, {1}, 1, ValueId{1}, pure, pure, normal, {1}},
      {ValueJump{}, {1}, 1, {2}, 0, {}, pure, pure, normal, {2}},
      {ValueCopy{}, {2}, 0, {2}, 1, ValueId{2}, pure, pure, normal, {3}},
      {ValueJump{}, {2}, 1, {3}, 0, {}, pure, pure, normal, {4}},
      {ValueReturn{}, {3}, 0, {3}, 1, {}, pure, pure, normal, {5}}};
  for (std::uint32_t i = 0; i < graph.ops.size(); ++i) graph.origins.push_back({{i}, {i}, {}});
  Append(graph, {OpId{0}, OperandRole::kCondition, ReadPurpose::kOperand, ValueId{0}, {}, {}, {}, {}});
  graph.uses.push_back({OpId{1}, OperandRole::kInput, ReadPurpose::kOperand, abcd::analysis::IntegerLiteral{11}, {}, {}, {}, {}});
  graph.uses.push_back({OpId{3}, OperandRole::kInput, ReadPurpose::kOperand, abcd::analysis::IntegerLiteral{22}, {}, {}, {}, {}});
  Append(graph, {OpId{5}, OperandRole::kReturnValue, ReadPurpose::kOperand, ValueId{3}, {}, {}, {}, {}});
  Append(graph, {ValueId{3}, OperandRole::kPhiInput, ReadPurpose::kOperand, ValueId{1}, EdgeId{2}, {}, {}, {}});
  Append(graph, {ValueId{3}, OperandRole::kPhiInput, ReadPurpose::kOperand, ValueId{2}, EdgeId{3}, {}, {}, {}});
  const auto evaluated = BuildEvaluationPlan(graph);
  Check(std::holds_alternative<EvaluationPlan>(evaluated));
  const auto& evaluation = std::get<EvaluationPlan>(evaluated);
  const std::vector<ExpressionConstant> constants(graph.ops.size());
  const auto tree = Tree(graph, evaluation, {constants, 2});
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(graph, control, evaluation, tree));
  Check(flow.copies.size() == 2 && std::get<EdgeId>(flow.copies[0].incoming) == EdgeId{2} &&
      std::get<EdgeId>(flow.copies[1].incoming) == EdgeId{3});
  Check(!flow.snapshots[1] && !flow.snapshots[3] && flow.phis[3]);
  for (std::size_t i = 0; i < flow.copies.size(); ++i) {
    const auto root = flow.copies[i].expressions[0];
    const auto& assignment = std::get<AssignmentExpr>(tree.expressions[root.value].expression);
    Check(std::get<SymbolTarget>(assignment.target).symbol == *flow.phis[3]);
    const auto producer = i == 0 ? 1U : 3U;
    Check(flow.expressions[producer] == assignment.value);
    const auto& literal = std::get<TreeLiteral>(tree.expressions[assignment.value.value].expression);
    Check(std::get<abcd::source::IntegerLiteral>(literal).value == (i == 0 ? 11 : 22));
  }
  auto changed = tree;
  auto& boundary = std::get<BlockBoundary>(changed.anchors[0].position);
  const auto& arm = std::get<BlockStmt>(changed.statements[boundary.block.value].statement);
  Check(arm.child_count == 1 && boundary.index == 0);
  ++boundary.index; // The jump anchor must still precede the incoming copy.
  Check(std::get<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation,
      changed, {constants, 2})) == TreeSemanticError::kFlow);
}
}  // namespace
int main() { Mutations(); EntryAndScaling(); PhiInputSnapshot(); EdgeSyntax(); DiamondSyntax(); }
