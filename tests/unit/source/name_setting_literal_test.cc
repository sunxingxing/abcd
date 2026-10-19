#include "../../../src/source/literal_plan.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
namespace {
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok) { std::cerr << "name-setting literal check at " << at.line() << '\n'; std::abort(); }
}
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
enum class ClosureHazard { kNone, kShared, kEnvironment, kHomeObject, kEscape };
ValueProgram ClosureLiteral(std::uint32_t fields, ClosureHazard hazard = ClosureHazard::kNone) {
  Fixture fixture;
  fixture.Add(ValueObjectAllocation{}, true, {});
  fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kAllocation;
  fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  std::optional<ValueId> first;
  for (std::uint32_t i = 0; i < fields; ++i) {
    fixture.Add(ValueFunctionCreation{CreationKind::kFunction, {1}, 0,
        hazard == ClosureHazard::kHomeObject ? HomeObjectInput::kAccumulator : HomeObjectInput::kCurrentFunction, 0}, true, {});
    auto& creation = fixture.graph.ops.back();
    creation.normal = creation.before_throw = LanguageEffect::kAllocation | LanguageEffect::kEnvironmentRead |
        LanguageEffect::kHeapRead | LanguageEffect::kModuleRead;
    creation.exceptional = ExceptionalCompletion::kMayThrow;
    const auto callback = *creation.result;
    if (!first) first = callback;
    if (hazard == ClosureHazard::kEscape) {
      fixture.Add(ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}});
      fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
      fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
    }
    if (hazard == ClosureHazard::kEnvironment) {
      fixture.Add(ValueEnvironmentPop{}, false, {});
      fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kEnvironmentWrite;
    }
    fixture.Add(ValueOwnWrite{OwnWriteKind::kStore, {i}, 0, PropertyNameSetting::kPropertyKey}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue,
          hazard == ClosureHazard::kShared ? *first : callback}});
    fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kHeapRead | LanguageEffect::kHeapWrite | LanguageEffect::kUserCode | LanguageEffect::kAllocation;
    fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  }
  fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{0}}});
  fixture.graph.blocks = {{{0}, static_cast<std::uint32_t>(fixture.graph.ops.size()), {0}, 0}};
  return std::move(fixture.graph);
}
void Proof() {
  for (const auto count : {1U, 200U, 201U, 400U, 800U}) {
    const auto graph = ClosureLiteral(count);
    const auto built = BuildLiteralPlan(graph);
    Check(std::holds_alternative<LiteralPlan>(built));
    const auto& plan = std::get<LiteralPlan>(built);
    Check(std::holds_alternative<std::uint64_t>(VerifyLiteralPlan(graph, plan)));
    auto missing = plan; missing.nested[1].reset();
    Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, missing)));
    auto wrong = graph;
    std::get<ValueFunctionCreation>(wrong.ops[1].operation).home_object = HomeObjectInput::kAccumulator;
    Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(wrong, plan)));
    wrong = graph; wrong.ops[2].operation = ValueNamedWrite{{0}};
    Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(wrong, plan)));
    wrong = graph; wrong.ops[1].operation = ValueGlobalRead{GlobalLookup::kName, {0}};
    Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(wrong, plan)));
    for (const bool work : {false, true}) {
      std::size_t lo = 0, hi = 64 * 1024 * 1024;
      const auto run = [&](std::size_t limit) {
        SemanticBudget budget; if (work) budget.work = limit; else budget.bytes = limit;
        return BuildLiteralPlan(graph, budget);
      };
      while (lo < hi) { const auto mid = lo + (hi-lo)/2;
        if (std::holds_alternative<LiteralPlan>(run(mid))) hi = mid; else lo = mid+1; }
      Check(lo && std::holds_alternative<LiteralPlan>(run(lo)));
      Check(std::holds_alternative<LiteralError>(run(lo-1)));
    }
  }
  for (const auto hazard : {ClosureHazard::kShared, ClosureHazard::kEnvironment, ClosureHazard::kHomeObject, ClosureHazard::kEscape})
    Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(ClosureLiteral(2, hazard))));

}
void TreeProof() {
  const auto graph = ClosureLiteral(2);
  const std::vector<FunctionBinding> functions{{OpId{1}, {1}}, {OpId{3}, {2}}};
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, {}, IncomingCells::kReject, functions));
  FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"first", u"second"};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  constants[2] = NameId{0}; constants[4] = NameId{1};
  const ExpressionConstants resolved{constants, syntax.names.size()};
  const auto constructed = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
  Check(std::holds_alternative<ConstructedFunction>(constructed));
  const auto& tree = std::get<ConstructedFunction>(constructed).tree;
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  auto wrong = tree; wrong.properties[0].name_setting = PropertyNameSetting::kNone;
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, resolved)));
  wrong = tree; wrong.properties[0].key = {1};
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, resolved)));
  wrong = tree; std::swap(wrong.properties[0].value, wrong.properties[1].value);
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, resolved)));
}

}
int main() { Proof(); TreeProof(); }
