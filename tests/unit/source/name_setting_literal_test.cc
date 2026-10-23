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
ValueProgram ClosureLiteral(std::uint32_t fields, ClosureHazard hazard = ClosureHazard::kNone, bool computed = false, bool copies = false) {
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
    auto callback = *creation.result;
    if (copies) {
      fixture.Add(ValueCopy{}, true, {{OperandRole::kInput, callback}});
      callback = *fixture.graph.ops.back().result;
    }
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
    if (computed) fixture.Add(ValueComputedOwnWrite{PropertyNameSetting::kPropertyKey}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kKey, abcd::analysis::IntegerLiteral{static_cast<std::int32_t>(i)}},
         {OperandRole::kStoredValue, hazard == ClosureHazard::kShared ? *first : callback}});
    else fixture.Add(ValueOwnWrite{OwnWriteKind::kStore, {i}, 0, PropertyNameSetting::kPropertyKey}, false,
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
void ComputedProof() {
  constexpr auto contract = SourceContract::kLanguageSourceV10;
  for (const auto count : {1U, 200U, 201U, 400U, 800U}) {
    auto graph = ClosureLiteral(count, ClosureHazard::kNone, true);
    auto split = ClosureLiteral(count, ClosureHazard::kNone, true, true);
    split.blocks.clear();
    for (std::uint32_t i=0; i<split.ops.size(); ++i) {
      const auto last=i+1==split.ops.size();split.ops[i].block={i};split.ops[i].order=0;
      split.blocks.push_back({{i},1,{i},static_cast<std::uint8_t>(last?0:1)});
      if(!last)split.edges.push_back({{i},{i+1},FlowEdgeKind::kFallthrough,0});
    }
    Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(split, {}, {}, {}, {}, nullptr, nullptr, contract)));
    const auto build = [&](SemanticBudget budget = {}) {
      return BuildLiteralPlan(graph, budget, {}, {}, {}, nullptr, nullptr, contract);
    };
    const auto built = build(); Check(std::holds_alternative<LiteralPlan>(built));
    const auto& plan = std::get<LiteralPlan>(built);
    const auto verify = [&](const ValueProgram& g, const LiteralPlan& p) {
      return VerifyLiteralPlan(g, p, {}, {}, {}, {}, nullptr, nullptr, contract);
    };
    Check(std::holds_alternative<std::uint64_t>(verify(graph, plan)));
    Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(graph)));
    auto wrong = graph;
    std::get<ValueComputedOwnWrite>(wrong.ops[2].operation).name_setting = PropertyNameSetting::kNone;
    Check(std::holds_alternative<LiteralError>(verify(wrong, plan)));
    wrong = graph; wrong.uses[wrong.ops[2].first_use.value+1].role = OperandRole::kStoredValue;
    Check(std::holds_alternative<LiteralError>(verify(wrong, plan)));
    wrong = graph; wrong.ops[2].operation = ValuePropertyWrite{};
    Check(std::holds_alternative<LiteralError>(verify(wrong, plan)));
    auto missing = plan; missing.nested[1].reset();
    Check(std::holds_alternative<LiteralError>(verify(graph, missing)));
    for (const bool work : {false, true}) {
      std::size_t lo=0, hi=64*1024*1024;
      const auto run=[&](std::size_t n) { SemanticBudget b; if(work)b.work=n;else b.bytes=n;return build(b); };
      while(lo<hi) {const auto mid=lo+(hi-lo)/2; if(std::holds_alternative<LiteralPlan>(run(mid)))hi=mid;else lo=mid+1;}
      Check(lo && std::holds_alternative<LiteralPlan>(run(lo)));
      Check(std::holds_alternative<LiteralError>(run(lo-1)));
      Check(std::get<LiteralError>(run(lo-1)) == (work ? LiteralError::kWorkLimit : LiteralError::kResourceLimit));
      if(count==800) std::cout << "computed 800 " << (work?"work":"bytes") << '=' << lo << '\n';
    }
  }
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
  const auto computed_graph = ClosureLiteral(2, ClosureHazard::kNone, true);
  ArraySourceInput source; source.contract=SourceContract::kLanguageSourceV10;
  const auto computed_evaluation = BuildEvaluationPlan(computed_graph, {}, IncomingCells::kReject, functions,
      {}, {}, {}, ClosureWrites::kUnknown, {}, {}, {}, nullptr, {}, source);
  Check(std::holds_alternative<EvaluationPlan>(computed_evaluation));
  syntax.identity.contract=source.contract;
  const auto computed_tree=BuildFunctionTree(computed_graph,control,std::get<EvaluationPlan>(computed_evaluation),syntax,resolved);
  Check(std::holds_alternative<ConstructedFunction>(computed_tree));
  const auto& actual=std::get<ConstructedFunction>(computed_tree).tree;
  const auto verify_tree=[&](const FunctionTree& t) {return VerifyTreeSemantics(computed_graph,control,std::get<EvaluationPlan>(computed_evaluation),t,resolved);};
  Check(std::holds_alternative<TreeSemantics>(verify_tree(actual)));
  wrong=actual; wrong.properties[0].computed_key.reset();
  Check(std::holds_alternative<TreeSemanticError>(verify_tree(wrong)));
  wrong=actual; std::swap(wrong.properties[0].computed_key,wrong.properties[1].computed_key);
  Check(std::holds_alternative<TreeSemanticError>(verify_tree(wrong)));
  wrong=actual; wrong.properties[0].name_setting=PropertyNameSetting::kNone;
  Check(std::holds_alternative<TreeSemanticError>(verify_tree(wrong)));

}

}
int main() { Proof(); ComputedProof(); TreeProof(); }
