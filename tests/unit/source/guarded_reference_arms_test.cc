#include "../../../src/source/call_plan.h"
#include "../../../src/source/logical_evaluation.h"
#include "../../../src/source/tree_checks.h"
#include "analysis/ssa_fixture.h"
#include <iostream>
#include "abcd/analysis/value_simplify.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
struct Graph {
  ValueProgram g;
  WorkBlockId block;
  void Block() {
    block = {static_cast<std::uint32_t>(g.blocks.size())};
    g.blocks.push_back({{static_cast<std::uint32_t>(g.ops.size())}, 0,
                       {static_cast<std::uint32_t>(g.edges.size())}, 0});
  }
  EdgeId Edge(WorkBlockId to, FlowEdgeKind kind) {
    EdgeId id{static_cast<std::uint32_t>(g.edges.size())};
    g.edges.push_back({block, to, kind, g.blocks[block.value].edge_count++});
    return id;
  }
  OpId Op(ValueOperation value,
          std::initializer_list<std::pair<OperandRole, ValueOperand>> inputs = {},
          bool result = false, LanguageEffect effect = LanguageEffect::kNone,
          bool throws = false) {
    OpId id{static_cast<std::uint32_t>(g.ops.size())};
    UseId first{static_cast<std::uint32_t>(g.uses.size())};
    for (auto [role, value] : inputs)
      g.uses.push_back({id, role, ReadPurpose::kOperand, value});
    std::optional<ValueId> out;
    if (result) {
      out = ValueId{static_cast<std::uint32_t>(g.values.size())};
      g.values.push_back({OpResult{id}, ValueKinds::Language()});
    }
    g.ops.push_back({value, block, g.blocks[block.value].op_count++, first,
        static_cast<std::uint32_t>(inputs.size()), out, effect,
        throws ? effect : LanguageEffect::kNone,
        throws ? ExceptionalCompletion::kMayThrow : ExceptionalCompletion::kImpossible,
        {id.value}});
    g.origins.push_back({{id.value}, {id.value}, {}});
    if (throws) g.exceptional_exits.push_back({id, {id.value}, effect, {}});
    return id;
  }
  ValueId Result(OpId op) { return *g.ops[op.value].result; }
  ValueOperand Copy(ValueId v, bool copies) {
    return copies ? ValueOperand{Result(Op(ValueCopy{}, {{OperandRole::kInput, v}}, true))}
                  : ValueOperand{v};
  }
  ValueProgram Finish() {
    for (std::uint32_t u = 0; u < g.uses.size(); ++u)
      if (const auto* value = std::get_if<ValueId>(&g.uses[u].operand)) {
        auto& head = g.values[value->value].first_use;
        g.uses[u].next = head;
        if (head) g.uses[head->value].previous = UseId{u};
        head = UseId{u};
      }
    return std::move(g);
  }
};
ValueProgram Build(unsigned count, bool copies = false, bool split = false,
                   bool intervening = false, bool member_reference = true, bool duplicate = false) {
  Graph b;
  b.g.values = {{EntryDefinition{ParameterEntry{0}}, ValueKinds::Language()},
                {EntryDefinition{ParameterEntry{1}}, ValueKinds::Language()}};
  b.Block();
  for (unsigned n = 0; n < count; ++n) {
    const auto member = b.Result(b.Op(ValueNamedRead{{0}}, {{OperandRole::kBase, ValueId{1}}},
        true, LanguageEffect::kHeapRead | LanguageEffect::kUserCode, true));
    const auto condition = b.Result(b.Op(ValueUnary{UnaryOperator::kToBoolean},
        {{OperandRole::kInput, ValueId{0}}}, true));
    b.g.values[condition.value].domain = ValueKinds::Boolean();
    b.Op(ValueBranch{UnaryOperator::kTrueOrNonzeroNumber}, {{OperandRole::kCondition, condition}});
    const auto header = b.block.value;
    const auto arm_blocks = split ? 2U : 1U;
    const WorkBlockId yes{header + 1}, no{header + 1 + arm_blocks}, join{header + 1 + 2 * arm_blocks};
    b.Edge(yes, FlowEdgeKind::kTaken); b.Edge(no, FlowEdgeKind::kFallthrough);
    std::pair<ValueId, EdgeId> arms[2];
    for (unsigned a = 0; a < 2; ++a) {
      b.Block();
      const auto read = b.Result(b.Op(ValueLexicalLoad{{{0}, {static_cast<std::uint16_t>(a)}}}, {}, true,
          LanguageEffect::kLexicalRead | LanguageEffect::kEnvironmentRead));
      const auto input = b.Copy(read, copies);
      if (split) { b.Edge({b.block.value + 1}, FlowEdgeKind::kFallthrough); b.Block(); }
      if (intervening) b.Op(ValueNamedRead{{0}}, {{OperandRole::kBase, ValueId{1}}}, true,
          LanguageEffect::kHeapRead | LanguageEffect::kUserCode, true);
      b.Op(ValueGuard{abc::PoolIndex{1}}, {{OperandRole::kGuardValue, input}}, false, LanguageEffect::kGuard, true);
      if (duplicate)
        b.Op(ValueGuard{abc::PoolIndex{1}}, {{OperandRole::kGuardValue, input}}, false, LanguageEffect::kGuard, true);
      arms[a] = {read, b.Edge(join, FlowEdgeKind::kFallthrough)};
    }
    b.Block();
    const ValueId merged{static_cast<std::uint32_t>(b.g.values.size())};
    b.g.values.push_back({PhiDefinition{join}, ValueKinds::Language()});
    for (auto [value, edge] : arms)
      b.g.uses.push_back({merged, OperandRole::kPhiInput, ReadPurpose::kOperand, value, edge});
    if (member_reference)
      b.Op(ValueInvoke{InvokeReceiver::kExplicit, 1}, {{OperandRole::kCallee, member},
          {OperandRole::kReceiver, ValueId{1}}, {OperandRole::kArgument, merged}}, true,
          LanguageEffect::kUserCode, true);
    else
      b.Op(ValueInvoke{InvokeReceiver::kUndefined, 1}, {{OperandRole::kCallee, member},
          {OperandRole::kArgument, merged}}, true, LanguageEffect::kUserCode, true);
  }
  b.Op(ValueReturn{}, {{OperandRole::kReturnValue, SpecialValue::kUndefined}});
  return b.Finish();
}
EvaluationPlan Plan(const ValueProgram& graph, SemanticBudget budget = {}) {
  auto result = BuildEvaluationPlan(graph, budget, IncomingCells::kConditional);
  if (const auto* e = std::get_if<EvaluationFailure>(&result)) std::cerr << "evaluation " << int(e->kind) << '\n';
  test::Check(std::holds_alternative<EvaluationPlan>(result));
  return std::get<EvaluationPlan>(std::move(result));
}
void Run(unsigned count, bool copies = false) {
  auto graph = Build(count, copies);
  test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
  const auto plan = Plan(graph);
  test::Check(std::holds_alternative<CallPlan>(BuildCallPlan(graph, plan)));
  auto bad = plan;
  bad.logical_values.clear();
  test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, bad)));
  if (count == 1) for (unsigned i = 0; i < graph.ops.size(); ++i) {
    if (plan.operations[i].kind == EvaluationKind::kCellGuard) {
      bad = plan; bad.operations[i].kind = EvaluationKind::kRoot;
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, bad)));
      bad = plan; bad.operations[i].kind = EvaluationKind::kElidedPure;
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, bad)));
      auto wrong = graph;
      for (auto& exit : wrong.exceptional_exits) if (exit.op == OpId{i})
        exit.handler = ValueHandler{{{0}, {0}, {0}}, {0}};
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(wrong, plan)));
    }
    if (plan.operations[i].kind != EvaluationKind::kPhiInput) continue;
    bad = plan; bad.operations[i].consumer = graph.ops[i].first_use;
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, bad)));
    bad = plan; bad.operations[i].consumer.reset();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, bad)));
  }
  test::Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(graph,
      {SemanticBudget{}.bytes, plan.work}, IncomingCells::kConditional)));
  test::Check(std::get<EvaluationFailure>(BuildEvaluationPlan(graph,
      {SemanticBudget{}.bytes, plan.work - 1}, IncomingCells::kConditional)) == EvaluationError::kWorkLimit);
  std::size_t low = 0, high = SemanticBudget{}.bytes;
  while (low + 1 < high) {
    const auto middle = low + (high - low)/2;
    if (std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(graph, {middle, SemanticBudget{}.work},
        IncomingCells::kConditional))) high = middle; else low = middle;
  }
  test::Check(std::get<EvaluationFailure>(BuildEvaluationPlan(graph, {high - 1, SemanticBudget{}.work},
      IncomingCells::kConditional)) == EvaluationError::kResourceLimit);
  std::cout << count << ' ' << plan.work << ' ' << high << '\n';
}
}
int main() {
  const auto foreign = Build(1, false, false, true);
  test::Check(std::holds_alternative<CallPlanFailure>(BuildCallPlan(foreign, Plan(foreign))));
  const auto duplicate = Build(1, false, false, false, true, true);
  test::Check(std::holds_alternative<CallPlanFailure>(BuildCallPlan(duplicate, Plan(duplicate))));
  const auto value_call = Build(1, false, false, false, false);
  const auto unchanged = Plan(value_call);
  TreeCheckBudget scratch({});
  const auto base = IndexPhiExpressionInputs(value_call, *unchanged.phis, {}, {}, scratch, unchanged.aliases);
  const auto seeded = IndexPhiExpressionInputs(value_call, *unchanged.phis, {}, {}, scratch, unchanged.aliases, &unchanged);
  test::Check(base == seeded);
  auto split = Build(1, true, true);
  test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(split, split.revision)));
  const auto split_plan = Plan(split);
  for (unsigned i = 0; i < split.ops.size(); ++i)
    if (std::holds_alternative<ValueLexicalLoad>(split.ops[i].operation))
      test::Check(split_plan.operations[i].kind == EvaluationKind::kPhiInput);
  Run(1); Run(1, true); for (auto n : {200U, 201U, 400U, 800U}) Run(n); }
