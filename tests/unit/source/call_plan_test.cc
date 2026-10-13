#include "../../../src/source/call_plan.h"

#include <cstdlib>
#include <cstdio>
#include <source_location>

namespace {
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool value, std::source_location where = std::source_location::current()) { if (!value) { std::fprintf(stderr, "call_plan_test:%u\n", where.line()); std::abort(); } }

class Fixture {
 public:
  Fixture() {
    for (std::uint32_t ordinal = 0; ordinal < 2; ++ordinal)
      graph.values.push_back({EntryDefinition{ParameterEntry{ordinal}}, ValueKinds::Language(), {}});
  }
  OpId Add(ValueOperation operation, bool result,
      std::initializer_list<std::pair<OperandRole, ValueOperand>> inputs) {
    const OpId id{static_cast<std::uint32_t>(graph.ops.size())};
    std::optional<ValueId> value;
    if (result) {
      value = ValueId{static_cast<std::uint32_t>(graph.values.size())};
      graph.values.push_back({OpResult{id}, ValueKinds::Language(), {}});
    }
    graph.origins.push_back({{id.value}, {10 + id.value}, abcd::abc::CodeOffset{100 + id.value}});
    graph.ops.push_back({operation, {0}, id.value,
        {static_cast<std::uint32_t>(graph.uses.size())}, static_cast<std::uint32_t>(inputs.size()),
        value, LanguageEffect::kUserCode, LanguageEffect::kUserCode,
        ExceptionalCompletion::kMayThrow, {id.value}});
    for (const auto& [role, operand] : inputs) {
      const UseId use{static_cast<std::uint32_t>(graph.uses.size())};
      OperandUse occurrence{id, role, ReadPurpose::kOperand, operand, {}, {}, {}, {}};
      if (const auto* input = std::get_if<ValueId>(&operand)) {
        auto& definition = graph.values[input->value];
        occurrence.next = definition.first_use;
        if (occurrence.next) graph.uses[occurrence.next->value].previous = use;
        definition.first_use = use;
      }
      graph.uses.push_back(occurrence);
    }
    return id;
  }
  EvaluationPlan Evaluation() {
    graph.blocks = {{{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0}};
    auto result = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<EvaluationPlan>(result));
    return std::get<EvaluationPlan>(std::move(result));
  }
  ValueProgram graph;
};

void Member(bool wrong_receiver, bool shared_callee) {
  Fixture fixture;
  fixture.Add(ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}});
  const auto call = fixture.Add(ValueInvoke{InvokeReceiver::kExplicit, 0}, true,
      {{OperandRole::kCallee, ValueId{2}},
       {OperandRole::kReceiver, ValueId{wrong_receiver ? 1U : 0U}}});
  if (shared_callee)
    fixture.Add(ValueInvoke{InvokeReceiver::kUndefined, 0}, true,
        {{OperandRole::kCallee, ValueId{2}}});
  const auto evaluation = fixture.Evaluation();
  auto result = BuildCallPlan(fixture.graph, evaluation);
  if (wrong_receiver || shared_callee) {
    const auto& failure = std::get<CallPlanFailure>(result);
    Check(failure == (shared_callee ? CallPlanError::kReference : CallPlanError::kReceiver));
    if (shared_callee) {
      Check(failure.pc == abcd::abc::CodeOffset{101} && failure.working_pc == abcd::abc::CodeOffset{11});
      Check(failure.reference && failure.reference->call == call && failure.reference->property == OpId{0});
      Check(failure.reference->kind == CallReferenceError::kNotInline &&
          failure.reference->placement == evaluation.operations[0].kind &&
          failure.reference->temporary == evaluation.operations[0].temporary &&
          failure.reference->consumer == evaluation.operations[0].consumer);
    }
    return;
  }
  auto plan = std::get<CallPlan>(std::move(result));
  Check(std::holds_alternative<MemberCall>(plan.operations[call.value]));
  Check(std::holds_alternative<std::uint64_t>(VerifyCallPlan(fixture.graph, evaluation, plan)));
  auto forged = plan;
  std::get<MemberCall>(forged.operations[call.value]).property = call;
  Check(std::get<CallPlanFailure>(VerifyCallPlan(fixture.graph, evaluation, forged)) == CallPlanError::kReference);
  forged = plan;
  ++std::get<MemberCall>(forged.operations[call.value]).receiver.value;
  Check(std::get<CallPlanFailure>(VerifyCallPlan(fixture.graph, evaluation, forged)) == CallPlanError::kBinding);
  forged = plan;
  ++forged.revision.value;
  Check(std::get<CallPlanFailure>(VerifyCallPlan(fixture.graph, evaluation, forged)) == CallPlanError::kRevision);
  SemanticBudget budget;
  budget.work = plan.work;
  Check(std::holds_alternative<CallPlan>(BuildCallPlan(fixture.graph, evaluation, budget)));
  --budget.work;
  Check(std::get<CallPlanFailure>(BuildCallPlan(fixture.graph, evaluation, budget)) == CallPlanError::kWorkLimit);
  budget = {};
  budget.bytes = 0;
  Check(std::get<CallPlanFailure>(BuildCallPlan(fixture.graph, evaluation, budget)) == CallPlanError::kResourceLimit);
}

void NonProperty() {
  Fixture fixture;
  const auto call = fixture.Add(ValueInvoke{InvokeReceiver::kExplicit, 0}, true,
      {{OperandRole::kCallee, ValueId{0}}, {OperandRole::kReceiver, ValueId{1}}});
  const auto evaluation = fixture.Evaluation();
  const auto failure = std::get<CallPlanFailure>(BuildCallPlan(fixture.graph, evaluation));
  Check(failure.kind == CallPlanError::kReference && failure.reference &&
      failure.reference->kind == CallReferenceError::kNotPropertyResult &&
      failure.reference->call == call && !failure.reference->property && !failure.reference->placement);
}
void ValueAndConstruction() {
  Fixture fixture;
  fixture.Add(ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}});
  const auto call = fixture.Add(ValueInvoke{InvokeReceiver::kUndefined, 0}, true,
      {{OperandRole::kCallee, ValueId{2}}});
  const auto construct = fixture.Add(ValueConstruct{0}, true,
      {{OperandRole::kConstructor, ValueId{1}}});
  const auto evaluation = fixture.Evaluation();
  auto result = BuildCallPlan(fixture.graph, evaluation);
  auto plan = std::get<CallPlan>(std::move(result));
  // A property-origin function value must not regain its property receiver.
  Check(std::get<ValueCall>(plan.operations[call.value]).eval == EvalMode::kIndirect);
  Check(std::holds_alternative<ConstructCall>(plan.operations[construct.value]));
  plan.operations[call.value] = ConstructCall{};
  Check(std::get<CallPlanFailure>(VerifyCallPlan(fixture.graph, evaluation, plan)) == CallPlanError::kBinding);
}
}  // namespace

int main() {
  Member(false, false);
  Member(true, false);
  Member(false, true);
  ValueAndConstruction();
  NonProperty();
}
