#include "call_plan.h"
#include "phase_evaluation.h"
#include "logical_evaluation.h"

#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = CallPlanError;
void Require(bool condition, Error error) { if (!condition) throw error; }
class CallBudget {
 public:
  explicit CallBudget(SemanticBudget budget) : initial_(budget), remaining_(budget) {}
  void Step(std::uint64_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit); remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= remaining_.bytes / width, Error::kResourceLimit); remaining_.bytes -= count * width;
  }
  void Evaluation(const ValueProgram& graph, const EvaluationPlan& evaluation, EvaluationFlow flow) {
    const auto checked = flow == EvaluationFlow::kNormal ? VerifyEvaluationPlan(graph, evaluation, remaining_)
        : VerifyPhasedEvaluationPlan(graph, evaluation, remaining_);
    if (const auto* work = std::get_if<std::uint64_t>(&checked)) { Step(*work); return; }
    const auto error = std::get<EvaluationError>(checked);
    if (error == EvaluationError::kResourceLimit) throw Error::kResourceLimit;
    if (error == EvaluationError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kEvaluation;
  }
  SemanticBudget Remaining() const { return remaining_; }
  std::uint64_t Work() const { return initial_.work - remaining_.work; }
 private:
  SemanticBudget initial_;
  SemanticBudget remaining_;
};
bool SameValue(const ValueOperand& left, const ValueOperand& right) {
  if (left.index() != right.index()) return false;
  return std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    const auto& other = std::get<T>(right);
    if constexpr (std::is_same_v<T, SpecialValue>) return value == other;
    else return value.value == other.value;
  }, left);
}
bool StableReceiver(const ValueProgram& graph, const EvaluationPlan& evaluation,
                    const ValueOperand& operand, UseId base) {
  const auto* id = std::get_if<ValueId>(&operand);
  if (!id) return true; // Primitive literal, with no property-key coercion here.
  const auto& definition = graph.values[id->value];
  if (const auto* entry = std::get_if<EntryDefinition>(&definition.origin))
    return std::holds_alternative<ParameterEntry>(*entry) || std::holds_alternative<ReceiverEntry>(*entry);
  // Fresh evaluation verification proves phi availability and edge copies.
  // This is a private provisional location, not a captured/mutable source cell.
  // The caller requires the same block, a checked logical diamond, or a no-copy phase corridor,
  // so no incoming-edge assignment can intervene between them.
  if (std::holds_alternative<PhiDefinition>(definition.origin))
    return evaluation.phis.has_value();
  const auto* result = std::get_if<OpResult>(&definition.origin);
  if (!result) return false;
  const auto& placement = evaluation.operations[result->op.value];
  return ((placement.kind == EvaluationKind::kRoot || placement.kind == EvaluationKind::kPredicateSnapshot) && placement.temporary.has_value()) ||
      ((placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kNestedLiteral) && placement.consumer == base);
}
bool Property(const ValueOperation& operation) {
  return std::holds_alternative<ValueNamedRead>(operation) || std::holds_alternative<ValuePropertyRead>(operation);
}
std::variant<std::uint64_t, CallPlanError> VerifyCalls(const ValueProgram& graph,
    const EvaluationPlan& evaluation, const CallPlan& plan, SemanticBudget budget, EvaluationFlow flow);
std::variant<CallPlan, CallPlanError> BuildCalls(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, analysis::SemanticBudget budget, EvaluationFlow flow) {
  try {
    CallBudget checked(budget);
    checked.Evaluation(graph, evaluation, flow);
    checked.Admit(1, sizeof(CallPlan));
    checked.Admit(graph.ops.size(), sizeof(CallBinding)); checked.Step(graph.ops.size());
    CallPlan plan{graph.revision, graph.cfg_revision, {}};
    plan.operations.resize(graph.ops.size());
    checked.Admit(plan.operations.capacity() - graph.ops.size(), sizeof(CallBinding));
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      checked.Step(); const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive || evaluation.operations[i].kind == EvaluationKind::kClassProjection) continue;
      if (std::holds_alternative<ValueConstruct>(op.operation)) {
        plan.operations[i] = ConstructCall{};
        continue;
      }
      const auto* invoke = std::get_if<ValueInvoke>(&op.operation);
      if (!invoke) continue;
      if (invoke->receiver == InvokeReceiver::kUndefined) {
        plan.operations[i] = ValueCall{};
        continue;
      }
      const auto& callee = graph.uses[op.first_use.value];
      const auto resolved = ResolveEvaluationOperand(evaluation, callee.operand);
      const auto* value = std::get_if<ValueId>(&resolved);
      Require(value != nullptr, Error::kCallee);
      const auto* result = std::get_if<OpResult>(&graph.values[value->value].origin);
      Require(result && Property(graph.ops[result->op.value].operation), Error::kReference);
      const UseId receiver{op.first_use.value + 1};
      plan.operations[i] = MemberCall{result->op, receiver};
    }
    // Independent checking below validates the proposed property/receiver
    // association, including the suppressed raw receiver occurrence.
    const auto verified = VerifyCalls(graph, evaluation, plan, checked.Remaining(), flow);
    if (const auto* error = std::get_if<Error>(&verified)) return *error;
    checked.Step(std::get<std::uint64_t>(verified));
    plan.allocated_bytes = sizeof(CallPlan) + plan.operations.capacity() * sizeof(CallBinding);
    plan.work = checked.Work();
    return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<std::uint64_t, CallPlanError> VerifyCalls(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const CallPlan& plan, analysis::SemanticBudget budget, EvaluationFlow flow) {
  try {
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    Require(plan.operations.size() == graph.ops.size(), Error::kBinding);
    CallBudget checked(budget);
    checked.Evaluation(graph, evaluation, flow);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      checked.Step(); const auto& op = graph.ops[i];
      const auto& binding = plan.operations[i];
      const auto* invoke = std::get_if<ValueInvoke>(&op.operation);
      if (op.life != ValueLife::kLive || evaluation.operations[i].kind == EvaluationKind::kClassProjection || (!invoke && !std::holds_alternative<ValueConstruct>(op.operation))) {
        Require(std::holds_alternative<NoCall>(binding), Error::kBinding); continue;
      }
      if (!invoke) { Require(std::holds_alternative<ConstructCall>(binding), Error::kBinding); continue; }
      if (invoke->receiver == InvokeReceiver::kUndefined) {
        const auto* value = std::get_if<ValueCall>(&binding);
        Require(value && value->eval == EvalMode::kIndirect, Error::kBinding); continue;
      }
      const auto* member = std::get_if<MemberCall>(&binding);
      Require(member && member->property.value < graph.ops.size() &&
          member->receiver.value == op.first_use.value + 1, Error::kBinding);
      const auto& property = graph.ops[member->property.value];
      const auto& placement = evaluation.operations[member->property.value];
      Require(property.life == ValueLife::kLive && Property(property.operation) && property.result &&
          ((property.block == op.block && property.order < op.order) ||
           (CrossPhaseInline(graph, evaluation, member->property) || CrossLogicalInline(graph, evaluation, member->property))), Error::kReference);
      Require((placement.kind == EvaluationKind::kInline ||
          (placement.kind == EvaluationKind::kNestedLiteral && evaluation.operations[i].kind == EvaluationKind::kNestedLiteral)) &&
          placement.consumer == op.first_use, Error::kReference);
      Require(SameValue(ResolveEvaluationOperand(evaluation, graph.uses[op.first_use.value].operand),
          *property.result), Error::kCallee);
      const auto base = ResolveEvaluationOperand(evaluation, graph.uses[property.first_use.value].operand);
      const auto receiver = ResolveEvaluationOperand(evaluation, graph.uses[member->receiver.value].operand);
      Require(SameValue(base, receiver) && StableReceiver(graph, evaluation, receiver, property.first_use),
          Error::kReceiver);
    }
    return checked.Work();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<CallPlan, CallPlanError> BuildCallPlan(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, analysis::SemanticBudget budget) {
  return BuildCalls(graph, evaluation, budget, EvaluationFlow::kNormal);
}
std::variant<std::uint64_t, CallPlanError> VerifyCallPlan(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const CallPlan& plan, analysis::SemanticBudget budget) {
  return VerifyCalls(graph, evaluation, plan, budget, EvaluationFlow::kNormal);
}
std::variant<CallPlan, CallPlanError> BuildPhasedCallPlan(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, analysis::SemanticBudget budget) {
  return BuildCalls(graph, evaluation, budget, EvaluationFlow::kPhased);
}
std::variant<std::uint64_t, CallPlanError> VerifyPhasedCallPlan(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const CallPlan& plan, analysis::SemanticBudget budget) {
  return VerifyCalls(graph, evaluation, plan, budget, EvaluationFlow::kPhased);
}
}  // namespace abcd::source
