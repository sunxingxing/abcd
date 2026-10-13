#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"

#include <cstdlib>
#include <utility>

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

ValueProgram Graph(bool computed, bool wrong_receiver) {
  ValueProgram graph;
  graph.values = {{EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}},
                  {EntryDefinition{ParameterEntry{1}}, ValueKinds::Language(), {}},
                  {PhiDefinition{{1}}, ValueKinds::Language(), {}},
                  {OpResult{{1}}, ValueKinds::Language(), {}},
                  {OpResult{{2}}, ValueKinds::Language(), {}}};
  graph.blocks = {{{0}, 1, {0}, 2}, {{1}, 3, {2}, 0}};
  graph.edges = {{{0}, {1}, FlowEdgeKind::kTaken, 0},
                 {{0}, {1}, FlowEdgeKind::kFallthrough, 1}};
  const auto effect = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
  const auto normal = LanguageEffect::kNone;
  const std::uint32_t property_uses = computed ? 2 : 1;
  const std::uint32_t call_first = 1 + property_uses;
  graph.ops = {
      {ValueBranch{UnaryOperator::kToBoolean}, {0}, 0, {0}, 1, {}, normal, normal,
       ExceptionalCompletion::kImpossible, {0}},
      {computed ? ValueOperation{ValuePropertyRead{}} : ValueOperation{ValueNamedRead{{0}}},
       {1}, 0, {1}, property_uses, ValueId{3}, effect, effect, ExceptionalCompletion::kMayThrow, {1}},
      {ValueInvoke{InvokeReceiver::kExplicit, 1}, {1}, 1, {call_first}, 3, ValueId{4},
       LanguageEffect::kUserCode, LanguageEffect::kUserCode, ExceptionalCompletion::kMayThrow, {2}},
      {ValueReturn{}, {1}, 2, {call_first + 3}, 1, {}, normal, normal,
       ExceptionalCompletion::kImpossible, {3}}};
  for (std::uint32_t i = 0; i < graph.ops.size(); ++i) graph.origins.push_back({{i}, {i}, {}});
  Append(graph, {OpId{0}, OperandRole::kCondition, ReadPurpose::kOperand, ValueId{0}, {}, {}, {}, {}});
  Append(graph, {OpId{1}, OperandRole::kBase, ReadPurpose::kOperand, ValueId{2}, {}, {}, {}, {}});
  if (computed)
    Append(graph, {OpId{1}, OperandRole::kKey, ReadPurpose::kOperand, ValueId{1}, {}, {}, {}, {}});
  Append(graph, {OpId{2}, OperandRole::kCallee, ReadPurpose::kOperand, ValueId{3}, {}, {}, {}, {}});
  Append(graph, {OpId{2}, OperandRole::kReceiver, ReadPurpose::kOperand,
      ValueId{wrong_receiver ? 1U : 2U}, {}, {}, {}, {}});
  Append(graph, {OpId{2}, OperandRole::kArgument, ReadPurpose::kOperand, ValueId{1}, {}, {}, {}, {}});
  Append(graph, {OpId{3}, OperandRole::kReturnValue, ReadPurpose::kOperand, ValueId{4}, {}, {}, {}, {}});
  Append(graph, {ValueId{2}, OperandRole::kPhiInput, ReadPurpose::kOperand, ValueId{0}, EdgeId{0}, {}, {}, {}});
  Append(graph, {ValueId{2}, OperandRole::kPhiInput, ReadPurpose::kOperand, ValueId{1}, EdgeId{1}, {}, {}, {}});
  return graph;
}

void PhiReceiver(bool computed) {
  const auto graph = Graph(computed, false);
  const auto evaluated = BuildEvaluationPlan(graph);
  Check(std::holds_alternative<EvaluationPlan>(evaluated));
  const auto& evaluation = std::get<EvaluationPlan>(evaluated);
  Check(evaluation.operations[1].kind == EvaluationKind::kInline);
  const auto calls = BuildCallPlan(graph, evaluation);
  Check(std::holds_alternative<CallPlan>(calls));
  Check(std::get<MemberCall>(std::get<CallPlan>(calls).operations[2]).property == OpId{1});
  const auto control = BuildControlPlan(graph);
  Check(std::holds_alternative<ControlPlan>(control));
  FunctionSyntax syntax{};
  syntax.callable = CallableKind::kMethod;
  syntax.strictness = Strictness::kStrict;
  syntax.names = {u"first", u"second", u"invoke"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}, {NameId{1}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  if (!computed) constants[1] = NameId{2};
  auto built = BuildFunctionTree(graph, std::get<ControlPlan>(control), evaluation,
      std::move(syntax), {constants, 3});
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto tree = std::get<ConstructedFunction>(std::move(built)).tree;
  const auto checked = VerifyTreeSemantics(graph, std::get<ControlPlan>(control), evaluation, tree, {constants, 3});
  Check(std::holds_alternative<TreeSemantics>(checked));
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(graph, std::get<ControlPlan>(control), evaluation, tree));
  const auto call_id = *flow.expressions[2];
  const auto& call = std::get<CallExpr>(tree.expressions[call_id.value].expression);
  Check(call.reference == CallReference::kMember && call.arguments.count == 1);
  const auto& member = std::get<MemberExpr>(tree.expressions[call.callee.value].expression);
  Check(std::get<SymbolExpr>(tree.expressions[member.base.value].expression).symbol == *flow.phis[2]);
  auto changed = tree;
  changed.expressions[member.base.value].expression = SymbolExpr{tree.parameters[0].symbol};
  Check(std::get<TreeSemanticError>(VerifyTreeSemantics(graph, std::get<ControlPlan>(control),
      evaluation, changed, {constants, 3})) == TreeSemanticError::kOperand);
  changed = tree;
  std::get<CallExpr>(changed.expressions[call_id.value].expression).reference = CallReference::kValue;
  Check(std::get<TreeSemanticError>(VerifyTreeSemantics(graph, std::get<ControlPlan>(control),
      evaluation, changed, {constants, 3})) == TreeSemanticError::kOperand);

  const auto wrong = Graph(computed, true);
  const auto wrong_evaluation = BuildEvaluationPlan(wrong);
  Check(std::holds_alternative<EvaluationPlan>(wrong_evaluation));
  Check(std::get<CallPlanFailure>(BuildCallPlan(wrong, std::get<EvaluationPlan>(wrong_evaluation))) == CallPlanError::kReceiver);
  auto invalid = graph;
  invalid.values[2].domain = ValueKinds::Hole();
  Check(std::get<CallPlanFailure>(BuildCallPlan(invalid, evaluation)) == CallPlanError::kEvaluation);
}
}  // namespace

int main() { PhiReceiver(false); PhiReceiver(true); }
