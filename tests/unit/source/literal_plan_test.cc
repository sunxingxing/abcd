#include "../../../src/analysis/value_flow.h"
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
    fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {i}, 0}, false,
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
ValueProgram StagedGraph(bool observed = false) {
  Fixture fixture;
  fixture.graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  fixture.Add(ValueObjectAllocation{abcd::abc::PoolIndex{0}}, true, {});
  fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kAllocation;
  fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  const auto read = [&](ValueId base) {
    fixture.Add(ValueNamedRead{{2}}, true, {{OperandRole::kBase, base}});
    auto& op = fixture.graph.ops.back();
    op.normal = op.before_throw = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
    op.exceptional = ExceptionalCompletion::kMayThrow;
  };
  if (observed) read({1});
  fixture.Add(ValueJump{}, false, {});
  const auto split = static_cast<std::uint32_t>(fixture.graph.ops.size());
  read({0});
  const auto rhs = *fixture.graph.ops.back().result;
  fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {0}, 0}, false,
      {{OperandRole::kBase, ValueId{1}}, {OperandRole::kStoredValue, rhs}});
  fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
  fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  // An independent nested literal in the later block must keep its local
  // interval proof; the existence of other CFG blocks is not a refusal reason.
  fixture.Add(ValueObjectAllocation{}, true, {});
  const auto nested = *fixture.graph.ops.back().result;
  fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kAllocation;
  fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  fixture.Add(ValueArrayAllocation{}, true, {});
  const auto array = *fixture.graph.ops.back().result;
  fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kAllocation;
  fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {0}, 0}, false,
      {{OperandRole::kBase, nested}, {OperandRole::kStoredValue, array}});
  fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
  fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{1}}});
  auto& graph = fixture.graph;
  graph.blocks = {{{0}, split, {0}, 1}, {{split}, static_cast<std::uint32_t>(graph.ops.size()) - split, {1}, 0}};
  graph.edges = {{{0}, {1}, FlowEdgeKind::kTaken, 0}};
  for (std::size_t i = split; i < graph.ops.size(); ++i) { graph.ops[i].block = {1}; graph.ops[i].order -= split; }
  return std::move(graph);
}
ValueProgram CyclicStaged(std::uint32_t fields, bool unfinished = false, bool repeat = false) {
  Fixture f;
  f.graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  f.Add(ValueObjectAllocation{abcd::abc::PoolIndex{0}}, true, {});
  f.graph.ops.back().normal = f.graph.ops.back().before_throw = LanguageEffect::kAllocation;
  f.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  if (unfinished) f.Add(ValueBranch{UnaryOperator::kToBoolean}, false, {{OperandRole::kCondition, ValueId{0}}});
  else f.Add(ValueJump{}, false, {});
  const auto split = static_cast<std::uint32_t>(f.graph.ops.size());
  for (std::uint32_t i = 0; i < fields; ++i) {
    f.Add(ValueNamedRead{{i}}, true, {{OperandRole::kBase, ValueId{0}}});
    const auto rhs = *f.graph.ops.back().result;
    f.graph.ops.back().normal = f.graph.ops.back().before_throw = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
    f.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
    f.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {i}, 0}, false,
        {{OperandRole::kBase, ValueId{1}}, {OperandRole::kStoredValue, rhs}});
    f.graph.ops.back().normal = f.graph.ops.back().before_throw = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
    f.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  }
  f.Add(ValueBranch{UnaryOperator::kToBoolean}, false, {{OperandRole::kCondition, ValueId{0}}});
  const auto end = static_cast<std::uint32_t>(f.graph.ops.size());
  f.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{1}}});
  auto& g = f.graph;
  g.blocks = {{{0}, split, {0}, static_cast<std::uint8_t>(unfinished ? 2 : 1)},
      {{split}, end - split, {static_cast<std::uint32_t>(unfinished ? 2 : 1)}, 2},
      {{end}, 1, {static_cast<std::uint32_t>(unfinished ? 4 : 3)}, 0}};
  g.edges.push_back({{0}, {1}, FlowEdgeKind::kTaken, 0});
  if (unfinished) g.edges.push_back({{0}, {0}, FlowEdgeKind::kFallthrough, 1});
  g.edges.push_back({{1}, {repeat ? 1U : 0U}, FlowEdgeKind::kTaken, 0});
  g.edges.push_back({{1}, {2}, FlowEdgeKind::kFallthrough, 1});
  for (std::uint32_t i = split; i < end; ++i) { g.ops[i].block = {1}; g.ops[i].order -= split; }
  g.ops[end].block = {2}; g.ops[end].order = 0;
  for (auto& op : g.ops) ++op.block.value;
  for (auto& edge : g.edges) { ++edge.source.value; ++edge.target.value; }
  for (auto& block : g.blocks) ++block.first_edge.value;
  g.edges.insert(g.edges.begin(), {{0}, {1}, FlowEdgeKind::kFallthrough, 0});
  g.blocks.insert(g.blocks.begin(), {{0}, 0, {0}, 1});
  return std::move(g);
}
void StagedActivations() {
  for (const auto fields : {1U, 200U, 201U, 400U, 800U}) {
    auto graph = CyclicStaged(fields);
    const auto flow = VerifyValueFlow(graph);
    Check(std::holds_alternative<ValueFlowProof>(flow));
    EvaluationInputSite failure;
    const auto built = BuildLiteralPlan(graph, {}, {}, {}, {}, nullptr, &failure);

    Check(std::holds_alternative<LiteralPlan>(built));
    const auto& plan = std::get<LiteralPlan>(built);
    const auto verified = VerifyLiteralPlan(graph, plan);
    Check(std::holds_alternative<std::uint64_t>(verified));
    SemanticBudget verification_budget;
    verification_budget.work = std::get<std::uint64_t>(verified);
    Check(std::holds_alternative<std::uint64_t>(VerifyLiteralPlan(graph, plan, verification_budget)));
    --verification_budget.work;
    Check(std::get<LiteralError>(VerifyLiteralPlan(graph, plan, verification_budget)) == LiteralError::kWorkLimit);
    for (auto changed : {CyclicStaged(fields, true), CyclicStaged(fields, false, true)}) {
      Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(changed)));
      EvaluationInputSite rejected;
      Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(changed, plan, {}, {}, {}, {}, nullptr, &rejected)));
      Check(rejected.check == EvaluationInputCheck::kLiteralStagedAcyclic);
    }
    auto exposed = graph;
    exposed.uses[exposed.ops[2].first_use.value].operand = ValueId{1};
    for (auto& value : exposed.values) value.first_use.reset();
    for (std::size_t u = 0; u < exposed.uses.size(); ++u) {
      auto& use = exposed.uses[u]; use.previous.reset(); use.next.reset();
      if (const auto* value = std::get_if<ValueId>(&use.operand)) {
        use.next = exposed.values[value->value].first_use;
        if (use.next) exposed.uses[use.next->value].previous = UseId{static_cast<std::uint32_t>(u)};
        exposed.values[value->value].first_use = UseId{static_cast<std::uint32_t>(u)};
      }
    }
    Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(exposed)));
    Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(exposed, plan)));
    SemanticBudget budget; budget.work = plan.work;
    Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph, budget)));
    --budget.work;
    Check(std::get<LiteralError>(BuildLiteralPlan(graph, budget)) == LiteralError::kWorkLimit);
    std::size_t low = 0, high = SemanticBudget{}.bytes;
    while (low < high) {
      const auto middle = low + (high - low) / 2;
      budget = {}; budget.bytes = middle;
      if (std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph, budget))) high = middle;
      else low = middle + 1;
    }
    budget = {}; budget.bytes = low;
    Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph, budget)));
    --budget.bytes;
    Check(std::get<LiteralError>(BuildLiteralPlan(graph, budget)) == LiteralError::kResourceLimit);
  }
}
// Staging retains the own-definition cursor and activation proof even when
// surrounding loops prevent a whole-method DAG proof.
void IndexedActivations() {
  const auto array = [](ValueProgram graph) {
    graph.ops[0].operation = ValueArrayAllocation{};
    std::uint32_t index = 0;
    for (auto& op : graph.ops)
      if (std::holds_alternative<ValueOwnWrite>(op.operation))
        op.operation = ValueIndexedWrite{index++, 0};
    return graph;
  };
  for (const auto count : {1U, 200U, 201U, 400U, 800U}) {
    const auto graph = array(CyclicStaged(count));
    Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(graph)));
    const auto built = BuildLiteralPlan(graph);
    Check(std::holds_alternative<LiteralPlan>(built));
    const auto& plan = std::get<LiteralPlan>(built);
    Check(plan.indexed_commits.size() == count);
    Check(
        std::holds_alternative<std::uint64_t>(VerifyLiteralPlan(graph, plan)));
    for (auto changed : {array(CyclicStaged(count, true)),
                         array(CyclicStaged(count, false, true))}) {
      auto candidate = plan;
      ArrayCursor cursor;
      for (std::size_t i = 0; i < candidate.indexed_commits.size(); ++i) {
        const auto step = std::get<ArrayInitializationStep>(
            IndexedArrayElement(changed, candidate.definitions[i], cursor));
        candidate.indexed_commits[i] = step;
        cursor = step.after;
      }
      EvaluationInputSite failure;
      Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(
          changed, candidate, {}, {}, {}, {}, nullptr, &failure)));
      Check(failure.check == EvaluationInputCheck::kLiteralStagedAcyclic);
    }
    auto changed = graph;
    const auto read = std::get<OpResult>(graph.values[2].origin).op;
    changed.uses[changed.ops[read.value].first_use.value].operand =
        *graph.ops[0].result;
    Check(
        std::holds_alternative<LiteralError>(VerifyLiteralPlan(changed, plan)));
  }
}
void StagedBuffer(bool print = false) {
  auto graph = StagedGraph();
  const auto planned = BuildLiteralPlan(graph);
  Check(std::holds_alternative<LiteralPlan>(planned));
  auto invalid = std::get<LiteralPlan>(planned); invalid.staged.clear();
  Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, invalid)));
  Check(std::get<LiteralError>(BuildLiteralPlan(StagedGraph(true))) == LiteralError::kObserved);
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value", u"field", u"probe"}; syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  constants[0] = AggregateConstant{0, 1}; constants[2] = NameId{2};
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (std::holds_alternative<ValueOwnWrite>(graph.ops[i].operation)) constants[i] = NameId{1};
  std::vector<ConstantProperty> properties{{NameId{1}, NullLiteral{}}};
  const ExpressionConstants resolved{constants, 3, 0, {}, properties};
  const auto built = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  auto wrong = tree;
  for (auto& node : wrong.expressions) if (auto* assign = std::get_if<AssignmentExpr>(&node.expression))
    if (auto* member = std::get_if<MemberTarget>(&assign->target)) member->key = NameId{2};
  Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, resolved)));
  auto missing = constants; missing[3] = NameId{2};
  Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(graph, control, evaluation, syntax,
      {missing, 3, 0, {}, properties})));
  std::ostringstream output;
  Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (print) std::cout << output.str();
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
  Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(Graph(1, Prefix::kLateValue))));
  Check(std::get<LiteralError>(BuildLiteralPlan(Graph(2, Prefix::kLateValue))) == LiteralError::kObserved);
  // Exact transport identity is not another evaluation of the parameter.
  Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(Graph(1, Prefix::kLateBinding))));
  Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(Graph(2, Prefix::kLateBinding))));
  auto buffered_observed = Graph(2, Prefix::kObserve);
  std::get<ValueObjectAllocation>(buffered_observed.ops[0].operation).literal = abcd::abc::PoolIndex{0};
  Check(std::get<LiteralError>(BuildLiteralPlan(buffered_observed)) == LiteralError::kObserved);
  auto buffered_late = Graph(2, Prefix::kLateValue);
  std::get<ValueObjectAllocation>(buffered_late.ops[0].operation).literal = abcd::abc::PoolIndex{0};
  Check(std::get<LiteralError>(BuildLiteralPlan(buffered_late)) == LiteralError::kObserved);
}
void ArrayBoundaries() {
  for (const auto prefix : {Prefix::kObserve, Prefix::kLateValue, Prefix::kLateBinding}) {
    auto graph = Graph(2, prefix); graph.ops[0].operation = ValueArrayAllocation{};
    std::uint32_t index = 0;
    for (auto& op : graph.ops) if (std::holds_alternative<ValueOwnWrite>(op.operation))
      op.operation = ValueIndexedWrite{index++, 0};
    if (prefix == Prefix::kLateBinding) Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph)));
    else Check(std::get<LiteralError>(BuildLiteralPlan(graph)) == LiteralError::kObserved);
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
void OrderedFields() {
  for (const auto count : {1U, 200U, 201U, 402U}) for (const bool reverse : {false, true}) {
    auto graph = Graph(count);
    const auto operations = static_cast<std::uint32_t>(graph.ops.size());
    std::vector<OpId> mapped(operations);
    for (std::uint32_t i = 0; i < operations; ++i) mapped[i] = {reverse && i ? operations-i : i};
    auto original = graph.ops;
    for (std::uint32_t i = 0; i < operations; ++i) {
      const auto target = mapped[i].value;
      graph.ops[target] = original[i]; graph.ops[target].block = {target}; graph.ops[target].order = 0;
    }
    for (auto& use : graph.uses) if (auto* owner = std::get_if<OpId>(&use.owner)) *owner = mapped[owner->value];
    graph.blocks.clear(); graph.edges.clear();
    for (std::uint32_t b = 0; b < operations; ++b) {
      const auto logical = reverse && b ? operations-b : b;
      const bool returned = logical+1 == operations;
      graph.blocks.push_back({{b},1,{static_cast<std::uint32_t>(graph.edges.size())},static_cast<std::uint8_t>(returned ? 0 : 1)});
      if (!returned) graph.edges.push_back({{b},{mapped[logical+1].value},FlowEdgeKind::kFallthrough,0});
    }
    const auto planned = BuildLiteralPlan(graph);
    Check(std::holds_alternative<LiteralPlan>(planned));
    const auto& literal = std::get<LiteralPlan>(planned);
    Check(literal.objects.size() == 1 && literal.objects[0].form == LiteralForm::kExpression);
    for (std::uint32_t i = 0; i < count; ++i) Check(literal.definitions[i] == mapped[i+1]);
    Check(std::holds_alternative<std::uint64_t>(VerifyLiteralPlan(graph,literal)));
    if (count > 1) {
      auto wrong = literal; std::swap(wrong.definitions[0],wrong.definitions[1]);
      Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph,wrong)));
    }
  }
}
enum class ClosureHazard { kNone, kShared, kEnvironment, kHomeObject };
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
    if (hazard == ClosureHazard::kEnvironment) {
      fixture.Add(ValueEnvironmentPop{}, false, {});
      fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kEnvironmentWrite;
    }
    fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {i}, 0}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue,
          hazard == ClosureHazard::kShared ? *first : callback}});
    fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
    fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  }
  fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{0}}});
  fixture.graph.blocks = {{{0}, static_cast<std::uint32_t>(fixture.graph.ops.size()), {0}, 0}};
  return std::move(fixture.graph);
}
void ClosureBoundaries() {
  std::uint64_t previous = 0;
  for (const auto count : {1U, 200U, 201U, 400U, 800U}) {
    auto graph = ClosureLiteral(count);
    const auto planned = BuildLiteralPlan(graph);
    Check(std::holds_alternative<LiteralPlan>(planned));
    const auto& plan = std::get<LiteralPlan>(planned);
    Check(plan.objects.size() == 1 && plan.definitions.size() == count);
    Check(std::holds_alternative<std::uint64_t>(VerifyLiteralPlan(graph, plan)));
    std::size_t creations = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i)
      if (std::holds_alternative<ValueFunctionCreation>(graph.ops[i].operation)) {
        Check(plan.nested[i].has_value()); ++creations;
      }
    Check(creations == count);
    if (count > 201) Check(plan.work < previous * 3);
    previous = plan.work;
    auto missing = plan; missing.nested[1].reset();
    Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, missing)));
    Check(std::holds_alternative<LiteralPlan>(BuildLiteralPlan(graph, {64 * 1024 * 1024, plan.work})));
    Check(std::get<LiteralError>(BuildLiteralPlan(graph, {64 * 1024 * 1024, plan.work - 1})) == LiteralError::kWorkLimit);
  }
  Check(std::get<LiteralError>(BuildLiteralPlan(ClosureLiteral(2, ClosureHazard::kShared))) == LiteralError::kObserved);
  Check(std::get<LiteralError>(BuildLiteralPlan(ClosureLiteral(1, ClosureHazard::kEnvironment))) == LiteralError::kObserved);
  Check(std::get<LiteralError>(BuildLiteralPlan(ClosureLiteral(1, ClosureHazard::kHomeObject))) == LiteralError::kLateValue);
}
void BufferedArraySuffix(bool print = false) {
  for (const std::uint32_t count : {1U, 200U, 201U, 402U}) {
    auto graph = Graph(count);
    graph.ops[0].operation = ValueArrayAllocation{abcd::abc::PoolIndex{0}};
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction;
    syntax.strictness = Strictness::kStrict;
    std::vector<ExpressionConstant> constants(graph.ops.size());
    constants[0] = AggregateConstant{0, 2, AggregateKind::kArray};
    for (std::uint32_t i = 0; i <= count; ++i) {
      const auto key = std::to_string(i); syntax.names.emplace_back(key.begin(), key.end());
      if (i) {
        graph.ops[i].operation = ValueIndexedWrite{i, 0};
        constants[i] = NameId{i};
      }
    }
    const std::vector<ConstantProperty> properties{{NameId{0}, NullLiteral{}}, {NameId{1}, NullLiteral{}}};
    const ExpressionConstants resolved{constants, syntax.names.size(), 0, {}, properties};
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    const auto built = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
    const auto* result = std::get_if<ConstructedFunction>(&built);
    Check(result && result->tree.properties.size() == count + 1);
    std::ostringstream output;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(result->tree, output)));
    Check(output.str().find("[null, 1") != std::string::npos);
    if (print) std::cout << output.str() << ",\n";
    auto changed = result->tree;
    changed.expressions[changed.properties.front().value.value].expression = TreeLiteral{UndefinedLiteral{}};
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control,
        evaluation, changed, resolved)));
    if (count > 1) {
      auto descending = graph;
      std::get<ValueIndexedWrite>(descending.ops[2].operation).index = 0;
      Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(descending)));
    }
  }
}
void SourceTree(bool buffered, bool array = false, bool print = false) {
  for (const std::uint32_t count : {1U, 2U, 200U, 201U}) {
    auto graph = Graph(count);
    if (array) {
      graph.ops[0].operation = ValueArrayAllocation{};
      for (std::uint32_t i = 0; i < count; ++i) graph.ops[i + 1].operation = ValueIndexedWrite{i, 0};
    }
    if (buffered) {
      if (array) std::get<ValueArrayAllocation>(graph.ops[0].operation).literal = abcd::abc::PoolIndex{0};
      else std::get<ValueObjectAllocation>(graph.ops[0].operation).literal = abcd::abc::PoolIndex{0};
    }
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
    if (buffered) constants[0] = AggregateConstant{0, prefix, array ? AggregateKind::kArray : AggregateKind::kObject};
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
    Check(result && result->tree.properties.size() == count);
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(
        graph, control, *evaluation, result->tree, resolved)));
    for (const auto& placement : result->tree.operations) {
      if (placement.operation.value == 0 || placement.operation.value > count) continue;
      Check(std::holds_alternative<PropertyId>(placement.node));
      const auto& property = result->tree.properties[std::get<PropertyId>(placement.node).value];
      Check(property.origin.value == graph.ops[placement.operation.value].origin.value);
    }
    if (!array) {
      auto printable = result->tree;
      printable.callable = CallableKind::kFunction;
      std::ostringstream output;
      const auto printed = std::get<PrintedFunction>(PrintFunctionTree(printable, output));
      const auto source = output.str();
      for (const auto range : printed.properties)
        Check(std::string_view(source).substr(range.begin, range.end - range.begin).starts_with("[\"__proto__\"]: "));
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
      if (buffered && count == 1) {
        // Index beyond the buffered prefix would introduce a hole; reject at
        // the constant-aware AST boundary, even though ordering alone is valid.
        std::get<ValueIndexedWrite>(sparse.ops[1].operation).index = 2;
        auto gap_constants = constants;
        auto gap_syntax = syntax; gap_syntax.names.push_back(u"2");
        gap_constants[1] = NameId{static_cast<std::uint32_t>(gap_syntax.names.size() - 1)};
        const ExpressionConstants gap_resolved{gap_constants, gap_syntax.names.size(), 0, {}, properties};
        const auto gap_evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(sparse));
        Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(sparse, control,
            gap_evaluation, gap_syntax, gap_resolved)));
      } else Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(sparse)));
      sparse = graph; sparse.ops[0].operation = ValueObjectAllocation{};
      Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(sparse)));
    }
    auto invalid_plan = *evaluation;
    invalid_plan.literals.reset();
    Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, invalid_plan)));
    invalid_plan = *evaluation;
    invalid_plan.operations[1].kind = EvaluationKind::kRoot;
    Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, invalid_plan)));
    auto changed = result->tree;
    changed.properties[0].origin = {0};
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
enum class BufferCase { kAscending, kDescending, kRepeatedKey };
void BufferedOverwrites(bool print = false) {
  for (const auto count : {2U, 200U, 201U, 402U})
      for (const auto mode : {BufferCase::kAscending, BufferCase::kDescending, BufferCase::kRepeatedKey}) {
    auto graph = Graph(count);
    std::get<ValueObjectAllocation>(graph.ops[0].operation).literal = abcd::abc::PoolIndex{0};
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction;
    syntax.strictness = Strictness::kStrict;
    std::vector<ConstantProperty> properties;
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto key = i == 0 ? std::string("__proto__") : "field" + std::to_string(i);
      syntax.names.emplace_back(key.begin(), key.end());
      properties.push_back({NameId{i}, NullLiteral{}});
    }
    // Distinct pool entries may spell the same name. Never compare their IDs
    // as the property identity, including repeated keys inside one buffer.
    for (std::uint32_t i = 0; i < count; ++i) syntax.names.push_back(syntax.names[i]);
    if (mode == BufferCase::kRepeatedKey) properties.push_back({NameId{count}, NullLiteral{}});
    constants[0] = AggregateConstant{0, static_cast<std::uint32_t>(properties.size())};
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto key = mode == BufferCase::kDescending ? count - 1 - i : i;
      constants[i + 1] = NameId{count + key};
      graph.uses[graph.ops[i + 1].first_use.value + 1].operand = abcd::analysis::IntegerLiteral{static_cast<std::int32_t>(i + 10)};
    }
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    const ExpressionConstants resolved{constants, syntax.names.size(), 0, {}, properties};
    const auto built = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(tree.properties.size() == (mode == BufferCase::kAscending ? count : mode == BufferCase::kDescending ? 2 * count - 1 : 2 * count));
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
    std::ostringstream output;
    const auto printed = std::get<PrintedFunction>(PrintFunctionTree(tree, output));
    if (print) std::cout << output.str() << ",\n";
    Check(printed.properties.size() == tree.properties.size());
    auto changed = tree;
    for (auto& expression : changed.expressions)
      if (auto* object = std::get_if<ObjectExpr>(&expression.expression)) --object->buffered;
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, changed, resolved)));
    changed = tree;
    // Relocating the first folded definition across a later equal buffer key
    // preserves all generic shape/ownership checks but changes the final value.
    if (mode == BufferCase::kRepeatedKey) {
      std::swap(changed.properties[0], changed.properties[count]);
      for (auto& placement : changed.operations)
        if (placement.operation == OpId{1}) placement.node = PropertyId{0};
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, changed, resolved)));
    }
    if (mode == BufferCase::kAscending) {
      std::swap(changed.properties[0], changed.properties[1]);
      for (auto& placement : changed.operations) {
        if (placement.operation == OpId{1}) placement.node = PropertyId{1};
        if (placement.operation == OpId{2}) placement.node = PropertyId{0};
      }
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, changed, resolved)));
    }
  }
}
enum class ConsumerCase { kUnique, kShared, kBarrier };
void LiteralConsumers(bool print = false) {
  for (const bool array : {false, true}) for (const auto count : {1U, 200U, 201U, 402U})
      for (const auto mode : {ConsumerCase::kUnique, ConsumerCase::kShared, ConsumerCase::kBarrier}) {
    Fixture fixture;
    fixture.Add(array ? ValueOperation{ValueArrayAllocation{}} : ValueOperation{ValueObjectAllocation{}}, true, {});
    fixture.graph.ops[0].normal = fixture.graph.ops[0].before_throw = LanguageEffect::kAllocation;
    fixture.graph.ops[0].exceptional = ExceptionalCompletion::kMayThrow;
    for (std::uint32_t i = 0; i < count; ++i) {
      fixture.Add(array ? ValueOperation{ValueIndexedWrite{i, 0}} :
          ValueOperation{ValueOwnWrite{OwnWriteKind::kDefineField, {i}, 0}}, false,
          {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, abcd::analysis::IntegerLiteral{1}}});
      auto& op = fixture.graph.ops.back();
      op.normal = op.before_throw = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
      op.exceptional = ExceptionalCompletion::kMayThrow;
    }
    if (mode == ConsumerCase::kBarrier) fixture.Add(ValueDebugger{}, false, {});
    if (mode == ConsumerCase::kShared)
      fixture.Add(ValueBinary{BinaryOperator::kStrictEqual}, true,
          {{OperandRole::kLeft, ValueId{0}}, {OperandRole::kRight, ValueId{0}}});
    fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue,
        ValueId{mode == ConsumerCase::kShared ? 1U : 0U}}});
    auto& graph = fixture.graph;
    graph.blocks = {{{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0}};
    const auto evaluated = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<EvaluationPlan>(evaluated));
    const auto& evaluation = std::get<EvaluationPlan>(evaluated);
    const auto& allocation = evaluation.operations[0];
    Check(allocation.kind == (mode == ConsumerCase::kUnique ? EvaluationKind::kInline : EvaluationKind::kRoot));
    if (mode == ConsumerCase::kUnique) Check(!allocation.temporary);
    else Check(allocation.temporary == (mode == ConsumerCase::kShared ?
        TemporaryReason::kMultipleUses : TemporaryReason::kEffectSnapshot));
    auto forged = evaluation;
    const auto consumer = mode == ConsumerCase::kShared ? graph.ops[count + 1].first_use : graph.ops.back().first_use;
    forged.operations[0] = {EvaluationKind::kInline, consumer, {}};
    if (mode != ConsumerCase::kUnique)
      Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
    forged.operations[0].consumer = graph.ops[1].first_use; // Internal receiver is never the escaping value.
    Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction;
    syntax.strictness = Strictness::kStrict;
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto key = (array ? std::string{} : std::string{"field"}) + std::to_string(i);
      syntax.names.emplace_back(key.begin(), key.end()); constants[i + 1] = NameId{i};
    }
    const auto built = BuildFunctionTree(graph, control, evaluation, syntax, {constants, count});
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    std::ostringstream output;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
    if (print) std::cout << output.str() << ",\n";
  }
}
void LateMemberCalls(bool print = false) {
  for (const std::uint32_t count : {1U, 2U, 200U, 201U, 402U}) {
    if (print && count != 2) continue;
    Fixture fixture;
    fixture.graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
    fixture.Add(ValueObjectAllocation{}, true, {});
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto base = static_cast<std::uint32_t>(fixture.graph.values.size());
      fixture.Add(ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}});
      fixture.Add(ValueNamedRead{{1}}, true, {{OperandRole::kBase, ValueId{base}}});
      fixture.Add(ValueInvoke{InvokeReceiver::kExplicit, 1}, true,
          {{OperandRole::kCallee, ValueId{base + 1}}, {OperandRole::kReceiver, ValueId{base}},
           {OperandRole::kArgument, ValueId{0}}});
      fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {i + 2}, 0}, false,
          {{OperandRole::kBase, ValueId{1}}, {OperandRole::kStoredValue, ValueId{base + 2}}});
    }
    fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{1}}});
    auto& graph = fixture.graph;
    graph.blocks = {{{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0}};
    for (auto& op : graph.ops) {
      if (std::holds_alternative<ValueReturn>(op.operation)) continue;
      op.normal = op.before_throw = std::holds_alternative<ValueObjectAllocation>(op.operation) ?
          LanguageEffect::kAllocation : LanguageEffect::kHeapRead | LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
      op.exceptional = ExceptionalCompletion::kMayThrow;
    }
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.parameters.push_back({{}, NameEvidence::kSynthesized});
    syntax.names = {u"method", u"bind"};
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto name = std::string{"field"} + std::to_string(i);
      syntax.names.emplace_back(name.begin(), name.end());
      constants[1 + i * 4] = NameId{0}; constants[2 + i * 4] = NameId{1};
      constants[4 + i * 4] = NameId{2 + i};
    }
    auto built = BuildFunctionTree(graph, control, evaluation, syntax, {constants, count + 2});
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, {constants, count + 2})));
    std::ostringstream source;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, source)));
    if (print) std::cout << source.str();
    auto forged = evaluation;
    forged.literals->nested[2].reset();
    Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
    if (count == 1) {
      // An explicit receiver different from the property base cannot become a
      // native member Reference, even though both values are available.
      auto changed = graph;
      changed.uses[changed.ops[3].first_use.value + 1].operand = ValueId{0};
      for (auto& value : changed.values) value.first_use.reset();
      for (auto& use : changed.uses) { use.previous.reset(); use.next.reset(); }
      for (std::size_t u = 0; u < changed.uses.size(); ++u) {
        auto& use = changed.uses[u];
        const auto* value = std::get_if<ValueId>(&use.operand);
        if (!value) continue;
        auto& definition = changed.values[value->value];
        use.next = definition.first_use;
        if (use.next) changed.uses[use.next->value].previous = UseId{static_cast<std::uint32_t>(u)};
        definition.first_use = UseId{static_cast<std::uint32_t>(u)};
      }
      const auto rejected = BuildEvaluationPlan(changed);
      Check(std::holds_alternative<EvaluationPlan>(rejected));
      Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(changed, control,
          std::get<EvaluationPlan>(rejected), syntax, {constants, count + 2})));
    }
  }
}
void ReadOrderBoundaries() {
  for (const bool reverse : {false, true}) for (const bool barrier : {false, true}) {
    Fixture fixture;
    fixture.Add(ValueObjectAllocation{}, true, {});
    fixture.Add(ValueNamedRead{abcd::abc::PoolIndex{0}}, true,
        {{OperandRole::kBase, abcd::analysis::IntegerLiteral{1}}});
    if (barrier) fixture.Add(ValueDebugger{}, false, {});
    fixture.Add(ValueNamedRead{abcd::abc::PoolIndex{0}}, true,
        {{OperandRole::kBase, abcd::analysis::IntegerLiteral{2}}});
    fixture.Add(ValuePropertyRead{}, true,
        {{OperandRole::kBase, ValueId{reverse ? 2U : 1U}}, {OperandRole::kKey, ValueId{reverse ? 1U : 2U}}});
    fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {0}, 0}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, ValueId{3}}});
    fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{0}}});
    auto& graph = fixture.graph;
    graph.blocks = {{{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0}};
    const auto plan = BuildLiteralPlan(graph);
    Check(std::holds_alternative<LiteralPlan>(plan) == (!reverse && !barrier));
  }
}
void LateModuleProperties(bool print = false) {
  for (const std::uint32_t count : {1U, 200U, 201U}) {
    Fixture fixture;
    fixture.Add(ValueObjectAllocation{}, true, {});
    std::vector<ModuleEvaluation> modules;
    std::vector<ExpressionConstant> constants(1);
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto read = static_cast<std::uint32_t>(fixture.graph.ops.size());
      const auto value = static_cast<std::uint32_t>(fixture.graph.values.size());
      fixture.Add(ValueModuleRead{ModuleSpace::kExternal, ModuleSlot{0}}, true, {});
      fixture.Add(ValueGuard{abcd::abc::PoolIndex{0}}, false, {{OperandRole::kGuardValue, ValueId{value}}});
      fixture.Add(ValueNamedRead{abcd::abc::PoolIndex{0}}, true, {{OperandRole::kBase, ValueId{value}}});
      fixture.Add(ValuePropertyRead{}, true, {{OperandRole::kBase, ValueId{value + 1}},
          {OperandRole::kKey, abcd::analysis::IntegerLiteral{static_cast<std::int32_t>(i)}}});
      fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {i}, 0}, false,
          {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, ValueId{value + 2}}});
      modules.push_back({{OpId{read}, OpId{read + 1}}, {ModuleSpace::kExternal, ModuleSlot{0}}, {{7}, {0}}});
      for (std::uint32_t j = 0; j < 5; ++j) {
        auto& op = fixture.graph.ops[read + j];
        op.normal = op.before_throw = j == 0 ? LanguageEffect::kModuleRead : j == 1 ?
            LanguageEffect::kGuard : j == 4 ? LanguageEffect::kHeapWrite : LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
        op.exceptional = ExceptionalCompletion::kMayThrow;
      }
      constants.resize(fixture.graph.ops.size());
      constants[read + 2] = NameId{0}; constants[read + 4] = NameId{i + 2};
    }
    fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{0}}});
    auto& graph = fixture.graph; constants.resize(graph.ops.size());
    graph.blocks = {{{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0}};
    graph.ops[0].normal = graph.ops[0].before_throw = LanguageEffect::kAllocation;
    graph.ops[0].exceptional = ExceptionalCompletion::kMayThrow;
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.names = {u"items", u"input"};
    syntax.modules.push_back({{{7}, {0}}, {1}, ModuleAccess::kReadOnly, NameEvidence::kMetadata});
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto key = "field" + std::to_string(i); syntax.names.emplace_back(key.begin(), key.end());
    }
    const ExpressionConstants resolved{constants, syntax.names.size(), 0};
    const auto planned = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, modules);
    Check(std::holds_alternative<EvaluationPlan>(planned));
    const auto& evaluation = std::get<EvaluationPlan>(planned);
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    const auto built = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    std::ostringstream output;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
    Check(output.str().find("$value") == std::string::npos);
    if (print) std::cout << output.str() << ",\n";
    auto forged = evaluation;
    forged.literals->nested[1].reset();
    Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
    if (count > 1) {
      forged = evaluation;
      std::swap(forged.literals->definitions[0], forged.literals->definitions[1]);
      Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
    }
  }
}
void LateStringLeaves(bool print = false) {
  for (const auto count : {1U, 200U, 201U, 402U}) {
    Fixture fixture;
    fixture.Add(ValueArrayAllocation{}, true, {});
    fixture.Add(ValueArrayAllocation{}, true, {});
    for (std::uint32_t i = 0; i < count; ++i) {
      fixture.Add(ValueString{abcd::abc::PoolIndex{i}}, true, {});
      fixture.Add(ValueIndexedWrite{i, 0}, false,
          {{OperandRole::kBase, ValueId{1}}, {OperandRole::kStoredValue, ValueId{i + 2}}});
    }
    fixture.Add(ValueIndexedWrite{0, 0}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, ValueId{1}}});
    fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{0}}});
    auto& graph = fixture.graph;
    graph.blocks = {{{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0}};
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.texts = {u"late"};
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto key = std::to_string(i); syntax.names.emplace_back(key.begin(), key.end());
      constants[2 + 2 * i] = TextId{0};
      constants[3 + 2 * i] = NameId{i};
    }
    constants[2 + 2 * count] = NameId{0};
    const ExpressionConstants resolved{constants, syntax.names.size(), syntax.texts.size()};
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    const auto built = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
    const auto* result = std::get_if<ConstructedFunction>(&built); Check(result);
    std::ostringstream output;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(result->tree, output)));
    Check(output.str().find("$value") == std::string::npos);
    if (print) std::cout << output.str() << ",\n";
    auto changed = result->tree;
    // Reject changing the leaf value while retaining its original operation.
    for (auto& expression : changed.expressions)
      if (std::holds_alternative<TreeLiteral>(expression.expression))
        expression.expression = TreeLiteral{UndefinedLiteral{}};
    Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, changed, resolved)));
    auto effectful = graph;
    effectful.ops[2].normal = LanguageEffect::kHeapWrite;
    Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(effectful)));
    auto forged = evaluation;
    forged.literals->nested[2].reset();
    Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
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
    Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
    forged = evaluation; forged.operations[depth].kind = EvaluationKind::kRoot;
    forged.operations[depth].consumer.reset();
    forged.operations[depth].temporary = TemporaryReason::kEffectSnapshot;
    Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
    forged = evaluation; forged.literals->nested.resize(1);
    Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
  }
  for (const bool observe : {false, true}) {
    Fixture fixture;
    fixture.Add(ValueArrayAllocation{}, true, {});
    fixture.Add(ValueObjectAllocation{}, true, {});
    if (observe) {
      fixture.Add(ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{1}}});
      fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
      fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
    }
    else fixture.Add(ValueDebugger{}, false, {});
    fixture.Add(ValueIndexedWrite{0, 0}, false,
        {{OperandRole::kBase, ValueId{0}}, {OperandRole::kStoredValue, ValueId{1}}});
    fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{0}}});
    fixture.graph.blocks = {{{0}, static_cast<std::uint32_t>(fixture.graph.ops.size()), {0}, 0}};
    Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(fixture.graph)));
  }
}
void MixedNestedTrees() {
  // Alternate forward object/property ownership and backward construct
  // operands. Neither one forward scan nor one reverse scan can lower this.
  for (const auto depth : {1U, 2U, 200U, 201U, 400U, 800U}) {
    Fixture fixture;
    fixture.graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
    for (std::uint32_t i = 0; i <= depth; ++i) {
      fixture.Add(ValueObjectAllocation{}, true, {});
      auto& op = fixture.graph.ops.back();
      op.normal = op.before_throw = LanguageEffect::kAllocation;
      op.exceptional = ExceptionalCompletion::kMayThrow;
    }
    for (std::uint32_t i = depth; i-- > 0;) {
      fixture.Add(ValueConstruct{1}, true,
          {{OperandRole::kConstructor, ValueId{0}}, {OperandRole::kArgument, ValueId{i + 2}}});
      auto& construct = fixture.graph.ops.back();
      construct.normal = construct.before_throw = LanguageEffect::kHeapRead | LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
      construct.exceptional = ExceptionalCompletion::kMayThrow;
      fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {0}, 0}, false,
          {{OperandRole::kBase, ValueId{i + 1}}, {OperandRole::kStoredValue, *construct.result}});
      auto& define = fixture.graph.ops.back();
      define.normal = define.before_throw = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
      define.exceptional = ExceptionalCompletion::kMayThrow;
    }
    fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{1}}});
    auto& graph = fixture.graph;
    graph.blocks = {{{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0}};
    const auto evaluated = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<EvaluationPlan>(evaluated));
    const auto& evaluation = std::get<EvaluationPlan>(evaluated);
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction;
    syntax.strictness = Strictness::kStrict; syntax.names = {u"Ctor", u"value"};
    syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i)
      if (std::holds_alternative<ValueOwnWrite>(graph.ops[i].operation)) constants[i] = NameId{1};
    const ExpressionConstants resolved{constants, 2};
    const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(tree.recovery_issues.empty());
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
    std::ostringstream output;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  }
}
void ReceiverGuardBoundaries() {
  Fixture fixture;
  fixture.graph.values.push_back({EntryDefinition{ReceiverEntry{}}, ValueKinds::Language(), {}});
  fixture.Add(ValueObjectAllocation{}, true, {});
  fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kAllocation;
  fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  fixture.Add(ValueSuperCheck{SuperCheckKind::kRequireInitialized}, false,
      {{OperandRole::kGuardValue, ValueId{0}}});
  fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kGuard;
  fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  fixture.graph.uses.back().purpose = ReadPurpose::kProtocol;
  fixture.Add(ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}});
  fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
  fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {0}, 0}, false,
      {{OperandRole::kBase, ValueId{1}}, {OperandRole::kStoredValue, ValueId{2}}});
  fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
  fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, ValueId{1}}});
  auto& graph = fixture.graph;
  graph.blocks = {{{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0}};
  const std::vector<OpId> guards{{1}};
  Check(std::get<LiteralError>(BuildLiteralPlan(graph)) == LiteralError::kObserved);
  const auto built = BuildLiteralPlan(graph, {}, {}, {}, guards);
  Check(std::holds_alternative<LiteralPlan>(built));
  const auto& plan = std::get<LiteralPlan>(built);
  Check(std::holds_alternative<std::uint64_t>(VerifyLiteralPlan(graph, plan, {}, {}, {}, guards)));
  Check(std::get<LiteralError>(VerifyLiteralPlan(graph, plan)) == LiteralError::kObserved);
  const auto evaluated = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, {}, {}, {},
      ClosureWrites::kUnknown, {}, guards);
  Check(std::holds_alternative<EvaluationPlan>(evaluated));
  auto evaluation = std::get<EvaluationPlan>(evaluated);
  Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, evaluation)));
  evaluation.receiver_guards.clear();
  Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, evaluation)));
  for (const std::vector<OpId> invalid : {std::vector<OpId>{{1}, {1}}, {{2}}, {{99}}, {{2}, {1}}})
    Check(std::get<LiteralError>(BuildLiteralPlan(graph, {}, {}, {}, invalid)) == LiteralError::kReceiver);
  std::get<ValueSuperCheck>(graph.ops[1].operation).kind = SuperCheckKind::kRequireUninitialized;
  Check(std::get<LiteralError>(BuildLiteralPlan(graph, {}, {}, {}, guards)) == LiteralError::kObserved);
  std::get<ValueSuperCheck>(graph.ops[1].operation).kind = SuperCheckKind::kRequireInitialized;
  graph.uses[graph.ops[1].first_use.value].purpose = ReadPurpose::kOperand;
  Check(std::get<LiteralError>(BuildLiteralPlan(graph, {}, {}, {}, guards)) == LiteralError::kReceiver);
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
  Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
}
enum class PredicateInput { kParameter, kBoolean, kLateRead, kEarlyRead };
ValueProgram PredicateGraph(PredicateInput kind) {
  Fixture fixture;
  fixture.graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  ValueId input{0};
  const auto read = [&] {
    fixture.Add(ValueNamedRead{{0}}, true, {{OperandRole::kBase, input}});
    input = *fixture.graph.ops.back().result;
    auto& op = fixture.graph.ops.back();
    op.normal = op.before_throw = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
    op.exceptional = ExceptionalCompletion::kMayThrow;
  };
  if (kind == PredicateInput::kEarlyRead) read();
  fixture.Add(ValueObjectAllocation{}, true, {});
  const auto allocation = *fixture.graph.ops.back().result;
  fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kAllocation;
  fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  if (kind == PredicateInput::kLateRead) read();
  if (kind == PredicateInput::kBoolean) {
    fixture.Add(ValueUnary{UnaryOperator::kLogicalNot}, true, {{OperandRole::kInput, input}});
    input = *fixture.graph.ops.back().result;
  }
  fixture.Add(ValueUnary{UnaryOperator::kFalseOrZero}, true, {{OperandRole::kInput, input}});
  const auto predicate = *fixture.graph.ops.back().result;
  fixture.Add(ValueOwnWrite{OwnWriteKind::kDefineField, {1}, 0}, false,
      {{OperandRole::kBase, allocation}, {OperandRole::kStoredValue, predicate}});
  fixture.graph.ops.back().normal = fixture.graph.ops.back().before_throw = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
  fixture.graph.ops.back().exceptional = ExceptionalCompletion::kMayThrow;
  fixture.Add(ValueReturn{}, false, {{OperandRole::kReturnValue, allocation}});
  fixture.graph.blocks = {{{0}, static_cast<std::uint32_t>(fixture.graph.ops.size()), {0}, 0}};
  return std::move(fixture.graph);
}
void PredicateLiterals(bool print = false) {
  for (const auto kind : {PredicateInput::kParameter, PredicateInput::kBoolean, PredicateInput::kEarlyRead}) {
    const auto graph = PredicateGraph(kind);
    const auto planned = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<EvaluationPlan>(planned));
    const auto& evaluation = std::get<EvaluationPlan>(planned);
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.names = {u"input", u"value", u"probe"}; syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      if (std::holds_alternative<ValueOwnWrite>(graph.ops[i].operation)) constants[i] = NameId{1};
      if (std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{2};
    }
    const ExpressionConstants resolved{constants, syntax.names.size()};
    const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
    Check(std::holds_alternative<ConstructedFunction>(built));
    const auto& tree = std::get<ConstructedFunction>(built).tree;
    Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
    std::ostringstream out; Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, out)));
    if (print) std::cout << out.str() << '\n';
  }
  auto graph = PredicateGraph(PredicateInput::kLateRead);
  Check(std::get<LiteralError>(BuildLiteralPlan(graph)) == LiteralError::kLateValue);
  const auto predicate = graph.ops.size() - 3;
  graph.ops[predicate].operation = ValueUnary{UnaryOperator::kLogicalNot};
  const auto planned = BuildLiteralPlan(graph);
  Check(std::holds_alternative<LiteralPlan>(planned));
  const auto plan = std::get<LiteralPlan>(planned);
  graph.ops[predicate].operation = ValueUnary{UnaryOperator::kFalseOrZero};
  Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, plan)));
  for (const ValueOperation suspended : {ValueOperation{ValueAwait{}}, ValueOperation{ValueYield{}}}) {
    graph.ops[predicate].operation = suspended;
    Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, plan)));
    Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(graph)));
  }
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--indexed-activations") {
    IndexedActivations();
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--staged-activations") { StagedActivations(); return 0; }
  StagedActivations(); OrderedFields();
  if (argc == 2 && std::string_view(argv[1]) == "--predicate-sources") { PredicateLiterals(true); return 0; }
  PredicateLiterals();
  if (argc == 2 && std::string_view(argv[1]) == "--staged-source") { StagedBuffer(true); return 0; }
  StagedBuffer(); ClosureBoundaries(); ReceiverGuardBoundaries();
  if (argc == 2 && std::string_view(argv[1]) == "--late-call-source") { LateMemberCalls(true); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--consumer-source") { LiteralConsumers(true); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--buffer-source") { BufferedOverwrites(true); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--array-buffer-source") { BufferedArraySuffix(true); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--late-string-source") { LateStringLeaves(true); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--late-read-source") { LateModuleProperties(true); return 0; }
  if (argc > 1) { SourceTree(false, true, true); return 0; }
  LateMemberCalls(); ReadOrderBoundaries(); LateModuleProperties(); LateStringLeaves(); LiteralConsumers(); BufferedArraySuffix(); BufferedOverwrites(); NestedLiterals(); MixedNestedTrees(); SourceTree(false, true); SourceTree(true, true); ArrayBoundaries(); Mutations(); Budgets(); SourceTree(false); SourceTree(true); RhsSnapshot(); }
