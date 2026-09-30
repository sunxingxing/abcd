#include "../../../src/source/literal_plan.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"

#include <cstdlib>
#include <utility>
#include <sstream>
#include <iostream>
#include "abcd/source/tree_printer.h"

namespace {
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool value) { if (!value) std::abort(); }
enum class Prefix { kNone, kObserve, kLateValue };
class Fixture {
 public:
  void Add(ValueOperation operation, bool result, std::initializer_list<std::pair<OperandRole, ValueOperand>> inputs) {
    const OpId id{static_cast<std::uint32_t>(graph.ops.size())};
    std::optional<ValueId> output;
    if (result) {
      output = ValueId{static_cast<std::uint32_t>(graph.values.size())};
      graph.values.push_back({OpResult{id}, ValueKinds::Language(), {}});
    }
    graph.origins.push_back({{id.value}, {id.value}, {}});
    graph.ops.push_back({operation, {0}, id.value, {static_cast<std::uint32_t>(graph.uses.size())},
        static_cast<std::uint32_t>(inputs.size()), output, LanguageEffect::kNone,
        LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {id.value}});
    for (const auto& [role, operand] : inputs) {
      const UseId use{static_cast<std::uint32_t>(graph.uses.size())};
      OperandUse occurrence{id, role, ReadPurpose::kOperand, operand, {}, {}, {}, {}};
      if (const auto* value = std::get_if<ValueId>(&operand)) {
        auto& definition = graph.values[value->value];
        occurrence.next = definition.first_use;
        if (occurrence.next) graph.uses[occurrence.next->value].previous = use;
        definition.first_use = use;
      }
      graph.uses.push_back(occurrence);
    }
  }
  ValueProgram graph;
};
ValueProgram Graph(std::uint32_t fields, Prefix prefix = Prefix::kNone) {
  Fixture fixture;
  fixture.Add(ValueObjectAllocation{}, true, {});
  fixture.graph.ops[0].normal = fixture.graph.ops[0].before_throw = LanguageEffect::kAllocation;
  fixture.graph.ops[0].exceptional = ExceptionalCompletion::kMayThrow;
  if (prefix != Prefix::kNone)
    fixture.Add(ValueCopy{}, true, {{OperandRole::kInput, prefix == Prefix::kObserve ?
        ValueOperand{ValueId{0}} : ValueOperand{abcd::analysis::IntegerLiteral{1}}}});
  for (std::uint32_t i = 0; i < fields; ++i) {
    fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {i}, 0}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue,
          prefix == Prefix::kLateValue ? ValueOperand{ValueId{1}} : ValueOperand{abcd::analysis::IntegerLiteral{1}}}});
    auto& op = fixture.graph.ops.back();
    op.normal = op.before_throw = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
    op.exceptional = ExceptionalCompletion::kMayThrow;
  }
  fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{0}}});
  fixture.graph.blocks = {{{0}, static_cast<std::uint32_t>(fixture.graph.ops.size()), {0}, 0}};
  return std::move(fixture.graph);
}
void Mutations() {
  auto graph = Graph(2);
  const auto built = BuildLiteralPlan(graph);
  const auto* plan = std::get_if<LiteralPlan>(&built);
  Check(plan && plan->objects.size() == 1 && plan->definitions.size() == 2);
  Check(std::holds_alternative<std::uint64_t>(VerifyLiteralPlan(graph, *plan)));
  auto changed = *plan;
  std::swap(changed.definitions[0], changed.definitions[1]);
  Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, changed)));
  changed = *plan; --changed.objects[0].count;
  Check(std::get<LiteralError>(VerifyLiteralPlan(graph, changed)) == LiteralError::kCoverage);
  changed = *plan; changed.definitions[1] = changed.definitions[0];
  Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, changed)));
  changed = *plan; ++changed.revision.value;
  Check(std::get<LiteralError>(VerifyLiteralPlan(graph, changed)) == LiteralError::kRevision);
  graph.ops[0].operation = ValueArrayAllocation{};
  Check(std::get<LiteralError>(BuildLiteralPlan(graph)) == LiteralError::kReceiver);
  graph = Graph(2); graph.values[0].domain = ValueKinds::Unknown();
  Check(std::get<LiteralError>(BuildLiteralPlan(graph)) == LiteralError::kDomain);
  Check(std::get<LiteralError>(BuildLiteralPlan(Graph(2, Prefix::kObserve))) == LiteralError::kObserved);
  Check(std::get<LiteralError>(BuildLiteralPlan(Graph(2, Prefix::kLateValue))) == LiteralError::kLateValue);
  auto buffered_observed = Graph(2, Prefix::kObserve);
  std::get<ValueObjectAllocation>(buffered_observed.ops[0].operation).literal = abcd::abc::PoolIndex{0};
  Check(std::get<LiteralError>(BuildLiteralPlan(buffered_observed)) == LiteralError::kObserved);
  auto buffered_late = Graph(2, Prefix::kLateValue);
  std::get<ValueObjectAllocation>(buffered_late.ops[0].operation).literal = abcd::abc::PoolIndex{0};
  Check(std::get<LiteralError>(BuildLiteralPlan(buffered_late)) == LiteralError::kLateValue);
}
void ArrayBoundaries() {
  for (const auto prefix : {Prefix::kObserve, Prefix::kLateValue}) {
    auto graph = Graph(2, prefix); graph.ops[0].operation = ValueArrayAllocation{};
    std::uint32_t index = 0;
    for (auto& op : graph.ops) if (std::holds_alternative<ValueOwnWrite>(op.operation))
      op.operation = ValueIndexedWrite{index++, 0};
    const auto expected = prefix == Prefix::kObserve ? LiteralError::kObserved : LiteralError::kLateValue;
    Check(std::get<LiteralError>(BuildLiteralPlan(graph)) == expected);
  }
}
void Budgets() {
  for (const std::uint32_t count : {0U, 1U, 2U, 200U, 201U, 4096U}) {
    const auto graph = Graph(count);
    const auto built = BuildLiteralPlan(graph);
    const auto* plan = std::get_if<LiteralPlan>(&built);
    Check(plan && plan->definitions.size() == count);
    abcd::analysis::SemanticBudget budget;
    budget.work = plan->work;
    Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph, budget)));
    --budget.work;
    Check(std::get<LiteralError>(BuildLiteralPlan(graph, budget)) == LiteralError::kWorkLimit);
    budget = {}; budget.bytes = 0;
    Check(std::get<LiteralError>(BuildLiteralPlan(graph, budget)) == LiteralError::kResourceLimit);
  }
}
void SourceTree(bool buffered, bool array = false, bool print = false) {
  for (const std::uint32_t count : {1U, 2U, 200U, 201U}) {
    auto graph = Graph(count);
    if (array) {
      graph.ops[0].operation = ValueArrayAllocation{};
      for (std::uint32_t i = 0; i < count; ++i) graph.ops[i + 1].operation = ValueIndexedWrite{i, 0};
    }
    if (buffered) std::get<ValueObjectAllocation>(graph.ops[0].operation).literal = abcd::abc::PoolIndex{0};
    const auto evaluated = BuildEvaluationPlan(graph);
    const auto* evaluation = std::get_if<EvaluationPlan>(&evaluated);
    Check(evaluation && evaluation->literals);
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    FunctionSyntax syntax{};
    syntax.callable = array ? CallableKind::kFunction : CallableKind::kMethod; syntax.strictness = Strictness::kStrict;
    // Duplicate computed keys retain their ordered own-data definitions; this
    // spelling must never acquire object-literal prototype-setter semantics.
    syntax.names = {u"__proto__"};
    std::vector<ExpressionConstant> constants(graph.ops.size());
    const std::vector<ConstantProperty> properties{{NameId{0}, NullLiteral{}}};
    const std::uint32_t prefix = buffered ? 1 : 0;
    if (buffered) constants[0] = ObjectConstant{0, prefix};
    for (std::size_t i = 1; i <= count; ++i) constants[i] = NameId{0};
    if (array) {
      syntax.names.clear();
      for (std::uint32_t i = 0; i < count; ++i) {
        const auto index = std::to_string(i); syntax.names.emplace_back(index.begin(), index.end());
        constants[i + 1] = NameId{i};
      }
    }
    const ExpressionConstants resolved{constants, syntax.names.size(), 0, {}, properties};
    const auto built = BuildFunctionTree(graph, control, *evaluation, syntax, resolved);
    const auto* result = std::get_if<ConstructedFunction>(&built);
    Check(result && result->tree.properties.size() == count + prefix);
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(
        graph, control, *evaluation, result->tree, resolved)));
    for (const auto& placement : result->tree.operations) {
      if (placement.operation.value == 0 || placement.operation.value > count) continue;
      Check(std::holds_alternative<PropertyId>(placement.node));
      const auto& property = result->tree.properties[std::get<PropertyId>(placement.node).value];
      Check(property.origin.value == graph.ops[placement.operation.value].origin.value);
    }
    if (array) {
      std::ostringstream output;
      Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(result->tree, output)));
      if (print && count == 2) std::cout << output.str();
      auto wrong_kind = result->tree;
      for (auto& expression : wrong_kind.expressions)
        if (auto* aggregate = std::get_if<ObjectExpr>(&expression.expression)) aggregate->kind = AggregateKind::kObject;
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, *evaluation, wrong_kind, resolved)));
      auto sparse = graph;
      ++std::get<ValueIndexedWrite>(sparse.ops[1].operation).index;
      Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(sparse)));
      sparse = graph; sparse.ops[0].operation = ValueObjectAllocation{};
      Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(sparse)));
    }
    auto invalid_plan = *evaluation;
    invalid_plan.literals.reset();
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, invalid_plan)));
    invalid_plan = *evaluation;
    invalid_plan.operations[1].kind = EvaluationKind::kRoot;
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, invalid_plan)));
    auto changed = result->tree;
    changed.properties[prefix].origin = {0};
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(
        graph, control, *evaluation, changed, resolved)));
    if (count > 1) {
      changed = result->tree;
      std::swap(changed.properties[0], changed.properties[1]);
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(
          graph, control, *evaluation, changed, resolved)));
      changed = result->tree;
      changed.properties[1].value = changed.properties[0].value;
      Check(std::get<TreeStructureError>(VerifyTreeStructure(changed)) == TreeStructureError::kOwnership);
    }
  }
}
void NestedLiterals() {
  for (const auto depth : {1U, 2U, 200U, 201U, 402U}) {
    Fixture fixture;
    for (std::uint32_t i = 0; i < depth; ++i) fixture.Add(ValueArrayAllocation{}, true, {});
    fixture.Add(ValueObjectAllocation{}, true, {});
    for (std::uint32_t i = depth; i-- > 0;)
      fixture.Add(ValueIndexedWrite{0, 0}, false,
          {{OperandRole::kBase, ValueId{i}}, {OperandRole::kStoredValue, ValueId{i + 1}}});
    fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{0}}});
    auto& graph = fixture.graph;
    graph.blocks = {{{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0}};
    for (std::size_t i = 0; i <= depth; ++i) {
      graph.ops[i].normal = graph.ops[i].before_throw = LanguageEffect::kAllocation;
      graph.ops[i].exceptional = ExceptionalCompletion::kMayThrow;
    }
    const auto evaluated = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<EvaluationPlan>(evaluated));
    const auto& evaluation = std::get<EvaluationPlan>(evaluated);
    Check(evaluation.literals && evaluation.literals->nested.size() == graph.ops.size());
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction;
    syntax.strictness = Strictness::kStrict; syntax.names = {u"0"};
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (std::uint32_t i = depth + 1; i <= 2 * depth; ++i) constants[i] = NameId{0};
    const ExpressionConstants resolved{constants, 1, 0};
    const auto built = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
    std::ostringstream output;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
    auto forged = evaluation;
    forged.literals->nested[depth].reset();
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
    forged = evaluation; forged.operations[depth].kind = EvaluationKind::kRoot;
    forged.operations[depth].consumer.reset();
    forged.operations[depth].temporary = TemporaryReason::kEffectSnapshot;
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
    forged = evaluation; forged.literals->nested.resize(1);
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
  }
  for (const bool observe : {false, true}) {
    Fixture fixture;
    fixture.Add(ValueArrayAllocation{}, true, {});
    fixture.Add(ValueObjectAllocation{}, true, {});
    if (observe) fixture.Add(ValueCopy{}, true, {{OperandRole::kInput, ValueId{1}}});
    else fixture.Add(ValueDebugger{}, false, {});
    fixture.Add(ValueIndexedWrite{0, 0}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, ValueId{1}}});
    fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{0}}});
    fixture.graph.blocks = {{{0}, static_cast<std::uint32_t>(fixture.graph.ops.size()), {0}, 0}};
    Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(fixture.graph)));
  }
}
void RhsSnapshot() {
  Fixture fixture;
  fixture.Add(ValueCopy{}, true, {{OperandRole::kInput, abcd::analysis::IntegerLiteral{1}}});
  fixture.Add(ValueObjectAllocation{}, true, {});
  fixture.graph.ops[1].normal = fixture.graph.ops[1].before_throw = LanguageEffect::kAllocation;
  fixture.graph.ops[1].exceptional = ExceptionalCompletion::kMayThrow;
  fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {0}, 0}, false,
      {{OperandRole::kBase, ValueId{1}}, {OperandRole::kStoredValue, ValueId{0}}});
  fixture.graph.ops[2].normal = fixture.graph.ops[2].before_throw =
      LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
  fixture.graph.ops[2].exceptional = ExceptionalCompletion::kMayThrow;
  fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{1}}});
  fixture.graph.blocks = {{{0}, 4, {0}, 0}};
  const auto& graph = fixture.graph;
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  Check(evaluation.operations[0].kind == EvaluationKind::kRoot &&
      evaluation.operations[0].temporary.has_value());
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{};
  syntax.callable = CallableKind::kMethod; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value"};
  std::vector<ExpressionConstant> constants(graph.ops.size()); constants[2] = NameId{0};
  const auto built = BuildFunctionTree(graph, control, evaluation, syntax, {constants, 1});
  const auto* result = std::get_if<ConstructedFunction>(&built);
  Check(result && result->tree.properties.size() == 1);
  Check(std::holds_alternative<SymbolExpr>(result->tree.expressions[
      result->tree.properties[0].value.value].expression));
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(
      graph, control, evaluation, result->tree, {constants, 1})));
  auto forged = evaluation;
  forged.operations[0].kind = EvaluationKind::kInline;
  forged.operations[0].temporary.reset();
  forged.operations[0].consumer = UseId{graph.ops[2].first_use.value + 1};
  Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
}
}  // namespace
int main(int argc, char**) {
  if (argc > 1) { SourceTree(false, true, true); return 0; }
  NestedLiterals(); SourceTree(false, true); ArrayBoundaries(); Mutations(); Budgets(); SourceTree(false); SourceTree(true); RhsSnapshot(); }
