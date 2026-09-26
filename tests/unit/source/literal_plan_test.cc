#include "../../../src/source/literal_plan.h"

#include <cstdlib>

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
}  // namespace
int main() { Mutations(); Budgets(); }
