#include "../../../src/source/literal_plan.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"

#include <cstdlib>
#include <utility>

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
        ValueOperand{ValueId{0}} : ValueOperand{IntegerLiteral{1}}}});
  for (std::uint32_t i = 0; i < fields; ++i) {
    fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {i}, 0}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue,
          prefix == Prefix::kLateValue ? ValueOperand{ValueId{1}} : ValueOperand{IntegerLiteral{1}}}});
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
void SourceTree() {
  for (const std::uint32_t count : {1U, 2U, 200U, 201U}) {
    const auto graph = Graph(count);
    const auto evaluated = BuildEvaluationPlan(graph);
    const auto* evaluation = std::get_if<EvaluationPlan>(&evaluated);
    Check(evaluation && evaluation->literals);
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    FunctionSyntax syntax{};
    syntax.callable = CallableKind::kMethod; syntax.strictness = Strictness::kStrict;
    // Duplicate computed keys retain their ordered own-data definitions; this
    // spelling must never acquire object-literal prototype-setter semantics.
    syntax.names = {u"__proto__"};
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (std::size_t i = 1; i <= count; ++i) constants[i] = NameId{0};
    const auto built = BuildFunctionTree(graph, control, *evaluation, syntax, {constants, 1});
    const auto* result = std::get_if<ConstructedFunction>(&built);
    Check(result && result->tree.properties.size() == count);
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(
        graph, control, *evaluation, result->tree, {constants, 1})));
    for (const auto& placement : result->tree.operations) {
      if (placement.operation.value == 0 || placement.operation.value > count) continue;
      Check(std::holds_alternative<PropertyId>(placement.node));
      const auto& property = result->tree.properties[std::get<PropertyId>(placement.node).value];
      Check(property.origin.value == graph.ops[placement.operation.value].origin.value);
    }
    auto invalid_plan = *evaluation;
    invalid_plan.literals.reset();
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, invalid_plan)));
    invalid_plan = *evaluation;
    invalid_plan.operations[1].kind = EvaluationKind::kRoot;
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, invalid_plan)));
    auto changed = result->tree;
    changed.properties[0].origin = {0};
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(
        graph, control, *evaluation, changed, {constants, 1})));
    if (count > 1) {
      changed = result->tree;
      std::swap(changed.properties[0], changed.properties[1]);
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(
          graph, control, *evaluation, changed, {constants, 1})));
      changed = result->tree;
      changed.properties[1].value = changed.properties[0].value;
      Check(std::get<TreeStructureError>(VerifyTreeStructure(changed)) == TreeStructureError::kOwnership);
    }
  }
}
void RhsSnapshot() {
  Fixture fixture;
  fixture.Add(ValueCopy{}, true, {{OperandRole::kInput, IntegerLiteral{1}}});
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
int main() { Mutations(); Budgets(); SourceTree(); RhsSnapshot(); }
