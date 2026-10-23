#include "call_plan.h"
#include "evaluation_liveness.h"
#include "function_object.h"
#include "phase_evaluation.h"
#include "logical_evaluation.h"

#include <new>
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
std::optional<analysis::OpId> MemberProperty(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, analysis::OpId call) {
  const auto& op = graph.ops[call.value];
  const auto* invoke = std::get_if<analysis::ValueInvoke>(&op.operation);
  if (op.life != analysis::ValueLife::kLive || plan.operations[call.value].kind == EvaluationKind::kClassProjection ||
      !invoke || invoke->receiver != analysis::InvokeReceiver::kExplicit || !op.use_count) return {};
  const auto operand = ResolveEvaluationOperand(plan, graph.uses[op.first_use.value].operand);
  const auto* value = std::get_if<analysis::ValueId>(&operand);
  const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
  if (!result) return {};
  const auto& property = graph.ops[result->op.value];
  if (property.life != analysis::ValueLife::kLive || !property.result ||
      (!std::holds_alternative<analysis::ValueNamedRead>(property.operation) &&
       !std::holds_alternative<analysis::ValuePropertyRead>(property.operation))) return {};
  return result->op;
}
bool MemberReferenceInline(const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::OpId call, analysis::OpId property) {
  const auto& placement = plan.operations[property.value];
  return (placement.kind == EvaluationKind::kInline ||
      (placement.kind == EvaluationKind::kNestedLiteral && plan.operations[call.value].kind == EvaluationKind::kNestedLiteral)) &&
      placement.consumer == graph.ops[call.value].first_use;
}
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
  void Evaluation(const ValueProgram& graph, const EvaluationPlan& evaluation, EvaluationFlow flow, const ForOfEvaluationInput* iteration) {
    const auto checked = flow == EvaluationFlow::kNormal ? VerifyEvaluationPlan(graph, evaluation, remaining_)
        : VerifyPhasedEvaluationPlan(graph, evaluation, remaining_, iteration);
    if (const auto* work = std::get_if<std::uint64_t>(&checked)) { Step(*work); return; }
    const auto error = std::get<EvaluationFailure>(checked);
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
  if (HasRetainedEvaluationValue(graph, evaluation, *id)) return true;
  // A for-in key read is the loop variable itself (bound per iteration).
  if (evaluation.for_in && id->value < evaluation.for_in->values.size() && evaluation.for_in->values[id->value] &&
      evaluation.for_in->values[id->value]->role == ForInRole::kNextResult) return true;
  const auto& definition = graph.values[id->value];
  const auto* result = std::get_if<OpResult>(&definition.origin);
  if (!result) return false;
  const auto& placement = evaluation.operations[result->op.value];
  return (placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kNestedLiteral) && placement.consumer == base;
}
bool Property(const ValueOperation& operation) {
  return std::holds_alternative<ValueNamedRead>(operation) || std::holds_alternative<ValuePropertyRead>(operation);
}
[[noreturn]] void ReferenceFailure(const ValueProgram& graph, const EvaluationPlan& evaluation,
    OpId call, std::optional<OpId> property, CallReferenceError reason) {
  CallPlanFailure failure{Error::kReference};
  failure.cause = reason;
  failure.reference = CallReferenceEvidence{reason, call, property, {}, {}, {}};
  if (property) {
    const auto& placement = evaluation.operations[property->value];
    failure.reference->placement = placement.kind;
    failure.reference->temporary = placement.temporary;
    failure.reference->consumer = placement.consumer;
  }
  const auto detail = std::lower_bound(evaluation.checked_failures.begin(), evaluation.checked_failures.end(), call,
      [](const GuardedCallFailure& value, OpId id) { return value.call < id; });
  if (detail != evaluation.checked_failures.end() && detail->call == call) failure.reference->checked = *detail;
  const auto origin = graph.ops[call.value].origin;
  if (origin.value < graph.origins.size()) {
    const auto& site = graph.origins[origin.value];
    failure.pc = site.original.value_or(site.pc); failure.working_pc = site.pc;
  }
  throw failure;
}
std::vector<std::size_t> ArrayUses(const ValueProgram& graph, const EvaluationPlan& evaluation,
    CallBudget& budget) {
  bool needed = false;
  for (const auto& op : graph.ops) {
    budget.Step(); needed |= op.life == ValueLife::kLive &&
        (std::holds_alternative<ValueConstructApply>(op.operation) ||
         (std::holds_alternative<ValueInvoke>(op.operation) && std::get<ValueInvoke>(op.operation).form == CallArgumentForm::kApplyArray));
  }
  if (!needed) return {};
  budget.Admit(graph.values.size(), sizeof(std::size_t)); budget.Step(graph.values.size());
  std::vector<std::size_t> counts(graph.values.size());
  budget.Admit(counts.capacity() - graph.values.size(), sizeof(std::size_t));
  for (const auto& use : graph.uses) {
    budget.Step();
    if (const auto* owner = std::get_if<OpId>(&use.owner)) {
      const auto kind = evaluation.operations[owner->value].kind;
      if (graph.ops[owner->value].life != ValueLife::kLive || kind == EvaluationKind::kAlias ||
          kind == EvaluationKind::kElidedPure) continue;
      // Fresh LiteralPlan checks these receiver occurrences as own definitions,
      // not observations. A stored self-reference is still counted separately.
      if (kind == EvaluationKind::kInitializer && use.role == OperandRole::kBase) continue;
      if (kind == EvaluationKind::kArrayCursor) continue;
      if (kind == EvaluationKind::kInitializer &&
          std::holds_alternative<ValueArraySpread>(graph.ops[owner->value].operation) &&
          use.role == OperandRole::kDestination) continue;
    }
    const auto canonical = ResolveEvaluationOperand(evaluation, use.operand);
    if (const auto* value = std::get_if<ValueId>(&canonical)) ++counts[value->value];
  }
  return counts;
}
OpId ApplyArray(const ValueProgram& graph, const EvaluationPlan& evaluation, const ValueOp& call,
    std::span<const std::size_t> uses) {
  Require(call.use_count == 2, Error::kArrayArguments);
  const UseId use{call.first_use.value + 1};
  const auto input = ResolveEvaluationOperand(evaluation, graph.uses[use.value].operand);
  const auto* value = std::get_if<ValueId>(&input);
  Require(value && value->value < graph.values.size() && value->value < uses.size() &&
      uses[value->value] == 1, Error::kArrayArguments);
  const auto* result = std::get_if<OpResult>(&graph.values[value->value].origin);
  Require(result && result->op.value < graph.ops.size(), Error::kArrayArguments);
  const auto& allocation = graph.ops[result->op.value];
  const auto& placement = evaluation.operations[result->op.value];
  // Fresh evaluation verification proves the canonical sole consumer, complete
  // literal interval and catch order. No repeated use-chain scan per argument.
  Require(allocation.life == ValueLife::kLive &&
      std::holds_alternative<ValueArrayAllocation>(allocation.operation) &&
      (((placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kNestedLiteral) &&
        placement.consumer == use) || (placement.kind == EvaluationKind::kRoot && placement.temporary)),
      Error::kArrayArguments);
  return result->op;
}
[[noreturn]] void SpreadFailure(const ValueProgram& graph, const ValueOp& op, CallSpreadError cause) {
  CallPlanFailure failure{Error::kArrayArguments}; failure.cause = cause;
  if (op.origin.value < graph.origins.size()) {
    const auto& site = graph.origins[op.origin.value];
    failure.pc = site.original.value_or(site.pc); failure.working_pc = site.pc;
  }
  throw failure;
}
std::optional<OpId> SpreadArray(const ValueProgram& graph, const EvaluationPlan& evaluation,
    const ValueOp& call, std::span<const std::size_t> uses) {
  const auto& invoke = std::get<ValueInvoke>(call.operation);
  if (invoke.form == CallArgumentForm::kRegisters) return {};
  if (CallSpread(evaluation.call_spread_contract) != CallSpreadSemantics::kPrivateArgumentsV1)
    SpreadFailure(graph, call, CallSpreadError::kPremise);
  if (invoke.receiver != InvokeReceiver::kExplicit || invoke.arguments != 1 || call.use_count != 3)
    SpreadFailure(graph, call, CallSpreadError::kCorrespondence);
  const UseId use{call.first_use.value + 2};
  const auto operand = ResolveEvaluationOperand(evaluation, graph.uses[use.value].operand);
  const auto* value = std::get_if<ValueId>(&operand);
  if (!value || value->value >= graph.values.size() || value->value >= uses.size())
    SpreadFailure(graph, call, CallSpreadError::kConstruction);
  if (uses[value->value] != 1) SpreadFailure(graph, call, CallSpreadError::kEscape);
  const auto* result = std::get_if<OpResult>(&graph.values[value->value].origin);
  if (!result || result->op.value >= graph.ops.size() ||
      !std::holds_alternative<ValueArrayAllocation>(graph.ops[result->op.value].operation))
    SpreadFailure(graph, call, CallSpreadError::kConstruction);
  const auto& placement = evaluation.operations[result->op.value];
  if ((placement.kind != EvaluationKind::kInline && placement.kind != EvaluationKind::kNestedLiteral) ||
      placement.consumer != use) SpreadFailure(graph, call, CallSpreadError::kOrder);
  // Fresh evaluation verification owns the original LiteralPlan/array-step
  // census, cursor equations, RHS order and catch correspondence. No new
  // opcode/block admission or repeated per-argument scans here.
  return result->op;
}
bool UndefinedReceiver(const ValueProgram& graph, const EvaluationPlan& plan, const ValueOp& op) {
  const auto input = ResolveEvaluationOperand(plan, graph.uses[op.first_use.value + 1].operand);
  const auto* constant = std::get_if<SpecialValue>(&input);
  return constant && *constant == SpecialValue::kUndefined;
}
OpId ApplyConstructor(const ValueProgram& graph, const EvaluationPlan& evaluation, const ValueOp& call) {
  const auto input = ResolveEvaluationOperand(evaluation, graph.uses[call.first_use.value].operand);
  const auto* value = std::get_if<ValueId>(&input);
  Require(value && value->value < graph.values.size(), Error::kConstructorIdentity);
  const auto* result = std::get_if<OpResult>(&graph.values[value->value].origin);
  // Runtime NewObject dispatches Proxy::ConstructInternal before checking
  // IsConstructor. A proxy of an arrow can run a trap that native `new` skips.
  // Creation-proven classes/functions have stable direct identity; arbitrary host
  // values/getters need further runtime modeling, never a lifetime assumption.
  Require(result && result->op.value < graph.ops.size() &&
      DirectFunctionObject(graph.ops[result->op.value].operation).has_value(), Error::kConstructorIdentity);
  return result->op;
}
std::variant<std::uint64_t, CallPlanFailure> VerifyCalls(const ValueProgram& graph,
    const EvaluationPlan& evaluation, const CallPlan& plan, SemanticBudget budget, EvaluationFlow flow, const ForOfEvaluationInput* iteration);
std::variant<CallPlan, CallPlanFailure> BuildCalls(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, analysis::SemanticBudget budget, EvaluationFlow flow, const ForOfEvaluationInput* iteration) {
  try {
    CallBudget checked(budget);
    checked.Evaluation(graph, evaluation, flow, iteration);
    checked.Admit(1, sizeof(CallPlan));
    checked.Admit(graph.ops.size(), sizeof(CallBinding)); checked.Step(graph.ops.size());
    CallPlan plan{graph.revision, graph.cfg_revision, {}};
    plan.operations.resize(graph.ops.size());
    checked.Admit(plan.operations.capacity() - graph.ops.size(), sizeof(CallBinding));
    const auto array_uses = ArrayUses(graph, evaluation, checked);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      checked.Step(); const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive || evaluation.operations[i].kind == EvaluationKind::kClassProjection || evaluation.operations[i].kind == EvaluationKind::kForOfEvent) continue;
      if (std::holds_alternative<ValueConstruct>(op.operation)) {
        plan.operations[i] = ConstructCall{};
        continue;
      }
      if (std::holds_alternative<ValueConstructApply>(op.operation)) {
        plan.operations[i] = ConstructCall{ApplyArray(graph, evaluation, op, array_uses),
            ApplyConstructor(graph, evaluation, op)};
        continue;
      }
      const auto* invoke = std::get_if<ValueInvoke>(&op.operation);
      if (!invoke) continue;
      const auto spread = SpreadArray(graph, evaluation, op, array_uses);
      if (invoke->receiver == InvokeReceiver::kUndefined || (spread && UndefinedReceiver(graph, evaluation, op))) {
        plan.operations[i] = ValueCall{EvalMode::kIndirect, spread};
        continue;
      }
      const auto& callee = graph.uses[op.first_use.value];
      const auto resolved = ResolveEvaluationOperand(evaluation, callee.operand);
      const auto* value = std::get_if<ValueId>(&resolved);
      Require(value != nullptr, Error::kCallee);
      const auto* result = std::get_if<OpResult>(&graph.values[value->value].origin);
      if (!result || !Property(graph.ops[result->op.value].operation))
        ReferenceFailure(graph, evaluation, {static_cast<std::uint32_t>(i)}, {},
            CallReferenceError::kNotPropertyResult);
      const UseId receiver{op.first_use.value + 1};
      plan.operations[i] = MemberCall{result->op, receiver, spread};
    }
    // Independent checking below validates the proposed property/receiver
    // association, including the suppressed raw receiver occurrence.
    const auto verified = VerifyCalls(graph, evaluation, plan, checked.Remaining(), flow, iteration);
    if (const auto* error = std::get_if<CallPlanFailure>(&verified)) return *error;
    checked.Step(std::get<std::uint64_t>(verified));
    plan.allocated_bytes = sizeof(CallPlan) + plan.operations.capacity() * sizeof(CallBinding);
    plan.work = checked.Work();
    return plan;
  } catch (const CallPlanFailure& failure) { return failure;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<std::uint64_t, CallPlanFailure> VerifyCalls(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const CallPlan& plan, analysis::SemanticBudget budget, EvaluationFlow flow, const ForOfEvaluationInput* iteration) {
  try {
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    Require(plan.operations.size() == graph.ops.size(), Error::kBinding);
    CallBudget checked(budget);
    checked.Evaluation(graph, evaluation, flow, iteration);
    const auto array_uses = ArrayUses(graph, evaluation, checked);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      checked.Step(); const auto& op = graph.ops[i];
      const auto& binding = plan.operations[i];
      const auto* invoke = std::get_if<ValueInvoke>(&op.operation);
      if (op.life != ValueLife::kLive || evaluation.operations[i].kind == EvaluationKind::kClassProjection || evaluation.operations[i].kind == EvaluationKind::kForOfEvent ||
          (!invoke && !std::holds_alternative<ValueConstruct>(op.operation) &&
           !std::holds_alternative<ValueConstructApply>(op.operation))) {
        Require(std::holds_alternative<NoCall>(binding), Error::kBinding); continue;
      }
      if (!invoke) {
        const auto* construct = std::get_if<ConstructCall>(&binding);
        Require(construct, Error::kBinding);
        if (std::holds_alternative<ValueConstructApply>(op.operation)) {
          Require(construct->array == ApplyArray(graph, evaluation, op, array_uses), Error::kArrayArguments);
          Require(construct->constructor == ApplyConstructor(graph, evaluation, op), Error::kConstructorIdentity);
        } else Require(!construct->array && !construct->constructor, Error::kBinding);
        continue;
      }
      const auto spread = SpreadArray(graph, evaluation, op, array_uses);
      if (invoke->receiver == InvokeReceiver::kUndefined || (spread && UndefinedReceiver(graph, evaluation, op))) {
        const auto* value = std::get_if<ValueCall>(&binding);
        Require(value && value->eval == EvalMode::kIndirect && value->array == spread, Error::kBinding); continue;
      }
      const auto* member = std::get_if<MemberCall>(&binding);
      Require(member && member->array == spread && member->property.value < graph.ops.size() &&
          member->receiver.value == op.first_use.value + 1, Error::kBinding);
      const auto& property = graph.ops[member->property.value];
      // Fresh evaluation verification owns availability and total effect order,
      // including selected regions and phases. CFG block placement is not a
      // Reference semantic: retain the original property result/base instead.
      if (!(property.life == ValueLife::kLive && Property(property.operation) && property.result))
        ReferenceFailure(graph, evaluation, {static_cast<std::uint32_t>(i)}, {},
            CallReferenceError::kNotPropertyResult);
      if (!MemberReferenceInline(graph, evaluation, {static_cast<std::uint32_t>(i)}, member->property))
        ReferenceFailure(graph, evaluation, {static_cast<std::uint32_t>(i)}, member->property,
            CallReferenceError::kNotInline);
      Require(SameValue(ResolveEvaluationOperand(evaluation, graph.uses[op.first_use.value].operand),
          *property.result), Error::kCallee);
      const auto base = ResolveEvaluationOperand(evaluation, graph.uses[property.first_use.value].operand);
      const auto receiver = ResolveEvaluationOperand(evaluation, graph.uses[member->receiver.value].operand);
      Require(SameValue(base, receiver) && StableReceiver(graph, evaluation, receiver, property.first_use),
          Error::kReceiver);
    }
    return checked.Work();
  } catch (const CallPlanFailure& failure) { return failure;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<CallArgumentUses, CallPlanError> IndexCallArgumentUses(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation, analysis::SemanticBudget budget) {
  try {
    CallBudget quota(budget); quota.Admit(1, sizeof(CallArgumentUses));
    auto uses = ArrayUses(graph, evaluation, quota);
    const auto bytes = sizeof(CallArgumentUses) + uses.capacity() * sizeof(std::size_t);
    return CallArgumentUses{std::move(uses), bytes, quota.Work()};
  } catch (CallPlanError error) { return error;
  } catch (const std::bad_alloc&) { return CallPlanError::kResourceLimit;
  } catch (const std::length_error&) { return CallPlanError::kResourceLimit; }
}
std::variant<CallPlan, CallPlanFailure> BuildCallPlan(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, analysis::SemanticBudget budget, const ForOfEvaluationInput* iteration) {
  return BuildCalls(graph, evaluation, budget, EvaluationFlow::kNormal, iteration);
}
std::variant<std::uint64_t, CallPlanFailure> VerifyCallPlan(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const CallPlan& plan, analysis::SemanticBudget budget, const ForOfEvaluationInput* iteration) {
  return VerifyCalls(graph, evaluation, plan, budget, EvaluationFlow::kNormal, iteration);
}
std::variant<CallPlan, CallPlanFailure> BuildPhasedCallPlan(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, analysis::SemanticBudget budget, const ForOfEvaluationInput* iteration) {
  return BuildCalls(graph, evaluation, budget, EvaluationFlow::kPhased, iteration);
}
std::variant<std::uint64_t, CallPlanFailure> VerifyPhasedCallPlan(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const CallPlan& plan, analysis::SemanticBudget budget, const ForOfEvaluationInput* iteration) {
  return VerifyCalls(graph, evaluation, plan, budget, EvaluationFlow::kPhased, iteration);
}
}  // namespace abcd::source
