#include "../../../src/source/literal_plan.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"

#include <cstdlib>
#include <source_location>
#include <utility>
#include <sstream>
#include <iostream>
#include "abcd/source/tree_printer.h"

namespace {
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool value, const std::source_location& where = std::source_location::current()) { if (!value) { std::cerr << "check failed at " << where.line() << '\n'; std::abort(); } }
enum class Prefix { kNone, kObserve, kLateValue, kLateBinding };
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
  if (prefix == Prefix::kLateBinding)
    fixture.graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  if (prefix == Prefix::kObserve) {
    fixture.Add(ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}});
    fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
    fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  } else if (prefix != Prefix::kNone)
    fixture.Add(ValueCopy{}, true, {{OperandRole::kInput,
        prefix == Prefix::kLateBinding ? ValueOperand{ValueId{1}} : ValueOperand{abcd::analysis::IntegerLiteral{1}}}});
  for (std::uint32_t i = 0; i < fields; ++i) {
    fixture.Add(ValueIndexedWrite{i * 7U, 0, IndexedDefinition::kDefineField}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue,
          prefix == Prefix::kLateBinding ? ValueOperand{ValueId{2}} :
          prefix == Prefix::kLateValue ? ValueOperand{ValueId{1}} : ValueOperand{abcd::analysis::IntegerLiteral{1}}}});
    auto& op = fixture.graph.ops.back();
    op.normal = op.before_throw = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
    op.exceptional = ExceptionalCompletion::kMayThrow;
  }
  fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{0}}});
  fixture.graph.blocks = {{{0}, static_cast<std::uint32_t>(fixture.graph.ops.size()), {0}, 0}};
  return std::move(fixture.graph);
}

void Cases() {
 for (const auto n : {1U, 200U, 201U, 400U, 800U}) {
  auto graph = Graph(n);
  const auto plan = std::get<LiteralPlan>(BuildLiteralPlan(graph));
  Check(plan.definitions.size() == n);
  Check(std::holds_alternative<std::uint64_t>(VerifyLiteralPlan(graph, plan)));
  auto invalid = plan; invalid.definitions[0] = {0};
  Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, invalid)));
  if (n > 1) {
   invalid = plan; std::swap(invalid.definitions[0], invalid.definitions[1]);
   Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, invalid)));
  }
  if (n == 1) {
   const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
   const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
   FunctionSyntax syntax{}; syntax.callable=CallableKind::kFunction; syntax.strictness=Strictness::kStrict;
   syntax.names={u"0",u"7"};
   std::vector<ExpressionConstant> constants(graph.ops.size()); constants[1]=NameId{0};
   const ExpressionConstants resolved{constants,2,0};
   const auto built=BuildFunctionTree(graph,control,evaluation,syntax,resolved);
   Check(std::holds_alternative<ConstructedFunction>(built));
   auto tree=std::get<ConstructedFunction>(built).tree;
   Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,evaluation,tree,resolved)));
   tree.properties[0].key={1};
   Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph,control,evaluation,tree,resolved)));
  }
  auto wrong = graph; wrong.ops[0].operation = ValueArrayAllocation{};
  Check(std::get<LiteralError>(BuildLiteralPlan(wrong)) == LiteralError::kReceiver);
  wrong = graph; std::get<ValueIndexedWrite>(wrong.ops[1].operation).kind = IndexedDefinition::kOwnStore;
  Check(std::get<LiteralError>(BuildLiteralPlan(wrong)) == LiteralError::kReceiver);
  SemanticBudget budget; budget.work = plan.work;
  Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph, budget)));
  --budget.work;
  Check(std::get<LiteralError>(BuildLiteralPlan(graph, budget)) == LiteralError::kWorkLimit);
  std::size_t low = 0, high = SemanticBudget{}.bytes;
  while (low < high) {
   const auto middle = low + (high-low)/2; budget = {}; budget.bytes = middle;
   if (std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph, budget))) high = middle;
   else low = middle + 1;
  }
  budget = {}; budget.bytes = low;
  Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph, budget)));
  --budget.bytes;
  Check(std::get<LiteralError>(BuildLiteralPlan(graph, budget)) == LiteralError::kResourceLimit);
  // Equivalent effect path split across one-operation blocks, with no local
  // adjacency or array-density admission. Keys remain 0, 7, 14, ... .
  graph.blocks.clear(); graph.edges.clear();
  for (std::uint32_t i = 0; i < graph.ops.size(); ++i) {
   auto& op = graph.ops[i]; op.block = {i}; op.order = 0;
   const bool last = i + 1 == graph.ops.size();
   graph.blocks.push_back({{i}, 1, {static_cast<std::uint32_t>(graph.edges.size())}, static_cast<std::uint8_t>(last ? 0 : 1)});
   if (!last) graph.edges.push_back({{i},{i+1},FlowEdgeKind::kFallthrough,0});
  }
  Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph)));
 }
 Check(std::get<LiteralError>(BuildLiteralPlan(Graph(2, Prefix::kObserve))) == LiteralError::kObserved);
 Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(Graph(2, Prefix::kLateBinding))));
}
}
int main() { Cases(); }
