#include "../../../src/source/phi_copies.h"
#include "../../../src/source/binding_plan.h"
#include "../../../src/source/expression_forest.h"

#include <bit>
#include <cstdlib>

namespace {
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
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
PhiCopies Plan(const ValueProgram& graph) {
  auto built = BuildPhiCopies(graph);
  Check(std::holds_alternative<PhiCopies>(built));
  auto plan = std::get<PhiCopies>(std::move(built));
  Check(std::holds_alternative<std::uint64_t>(VerifyPhiCopies(graph, plan)));
  return plan;
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
      const auto plan = Plan(graph);
      Check(plan.bundles.size() == 2);
      Check(plan.bundles[0].count == count && plan.bundles[1].count == count);
      Check(std::holds_alternative<MethodEntryKey>(plan.bundles[0].incoming) == loop);
      Check(plan.bundles[0].schedule.temporaries == 0);
      Check(plan.bundles[1].schedule.temporaries == (loop && count > 1 ? 1U : 0U));
      const auto evaluated = BuildEvaluationPlan(graph);
      Check(std::holds_alternative<EvaluationPlan>(evaluated));
      const auto& evaluation = std::get<EvaluationPlan>(evaluated);
      Check(evaluation.phis && evaluation.phis->bundles.size() == 2);
      const auto bound = BuildBindingPlan(graph, evaluation, 1);
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
      Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, missing)) == EvaluationError::kLivePhi);
      constexpr std::uint64_t kWorkPerInputLogBound = 512;
      Check(plan.work <= kWorkPerInputLogBound * graph.uses.size() * std::bit_width(graph.uses.size()));
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
  const std::vector<ExpressionConstant> constants{NameId{0}, std::monostate{}, std::monostate{}};
  const auto syntax = BuildExpressionForest(graph, evaluation, 1, {0}, {constants, 1});
  Check(std::holds_alternative<ExpressionForest>(syntax));
  const auto& forest = std::get<ExpressionForest>(syntax);
  const auto& copied = std::get<AssignmentExpr>(
      forest.expressions[forest.edge_assignments[forest.edges[0].first].value].expression);
  Check(std::get<SymbolTarget>(copied.target).symbol == phi);
  Check(std::get<SymbolExpr>(forest.expressions[copied.value.value].expression).symbol == snapshot);
  // The getter remains at its operation root, not duplicated on either edge.
  Check(std::holds_alternative<MemberExpr>(forest.expressions[forest.operations[0]->value].expression));
  auto forged = evaluation;
  forged.operations[0] = {EvaluationKind::kInline, UseId{3}, {}};
  Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, forged)) == EvaluationError::kOwnership);
  forged = evaluation;
  forged.phis->bundles[0].schedule.steps.clear();
  Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, forged)) == EvaluationError::kLivePhi);
}
void EdgeSyntax() {
  for (const std::uint32_t count : {1U, 2U, 200U, 201U, 4096U}) {
    auto graph = Graph(count, false);
    graph.origins = {{{0}, {0}, {}}, {{1}, {1}, {}}};
    const auto evaluated = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<EvaluationPlan>(evaluated));
    const auto& evaluation = std::get<EvaluationPlan>(evaluated);
    const std::vector<ExpressionConstant> constants(graph.ops.size());
    const auto built = BuildExpressionForest(graph, evaluation, 1, {0}, {constants});
    Check(std::holds_alternative<ExpressionForest>(built));
    const auto& forest = std::get<ExpressionForest>(built);
    Check(forest.edges.size() == 2 && forest.edge_assignments.size() == count * 2);
    std::vector<bool> owned(forest.expressions.size());
    for (std::uint32_t edge = 0; edge < 2; ++edge) {
      const auto& range = forest.edges[edge];
      Check(range.edge == EdgeId{edge} && range.count == count && range.first == edge * count);
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
  }
  auto entry = Graph(2, true);
  entry.origins = {{{0}, {0}, {}}, {{1}, {1}, {}}};
  const auto evaluated = BuildEvaluationPlan(entry);
  Check(std::holds_alternative<EvaluationPlan>(evaluated));
  const std::vector<ExpressionConstant> constants(entry.ops.size());
  Check(std::get<ExpressionError>(BuildExpressionForest(entry,
      std::get<EvaluationPlan>(evaluated), 1, {0}, {constants})) == ExpressionError::kUnsupportedOperation);
}
}  // namespace
int main() { Mutations(); EntryAndScaling(); PhiInputSnapshot(); EdgeSyntax(); }
