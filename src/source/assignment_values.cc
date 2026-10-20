#include "assignment_values.h"
#include "evaluation_aliases.h"
namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool ok) { if (!ok) throw EvaluationError::kOwnership; }
// Exactly one write and one argument may share this occurrence. Additional
// observations require separate materialization, never duplicated evaluation.
constexpr std::uint32_t kStoreAndArgumentUses = 2;
constexpr std::uint32_t kExtraObservation = kStoreAndArgumentUses + 1;
struct Observations { std::uint32_t count = 0; std::optional<UseId> store, argument; };
std::vector<Observations> Census(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  std::vector<Observations> counts; budget.Array(counts, graph.values.size());
  for (std::size_t u = 0; u < graph.uses.size(); ++u) {
    budget.Step(); const auto& use = graph.uses[u];
    if (use.life != ValueLife::kLive || IsIdentityPhiInput(plan, use)) continue;
    const auto* owner = std::get_if<OpId>(&use.owner);
    if (owner && (graph.ops[owner->value].life != ValueLife::kLive ||
        plan.operations[owner->value].kind == EvaluationKind::kAlias ||
        plan.operations[owner->value].kind == EvaluationKind::kElidedPure)) continue;
    const auto operand = ResolveEvaluationOperand(plan, use.operand);
    const auto* value = std::get_if<ValueId>(&operand);
    if (!value || !graph.values[value->value].domain.IsLanguage()) continue;
    auto& item = counts[value->value]; if (item.count < kExtraObservation) ++item.count;
    if (!owner) continue;
    const auto& op = graph.ops[owner->value]; const auto shape = DescribeEvaluation(op.operation);
    if (shape && shape->assignment_result == use.role && !op.result &&
        use.purpose == ReadPurpose::kOperand) item.store = UseId{static_cast<std::uint32_t>(u)};
    if (use.role == OperandRole::kArgument && std::holds_alternative<ValueInvoke>(op.operation))
      item.argument = UseId{static_cast<std::uint32_t>(u)};
  }
  return counts;
}
}
std::optional<analysis::OpId> AssignmentProducer(const EvaluationPlan& plan, analysis::UseId use) {
  if(use.value<plan.member_updates.results.size() && plan.member_updates.results[use.value])return plan.member_updates.results[use.value];
  return use.value < plan.assignment_values.size() ? plan.assignment_values[use.value] : std::nullopt;
}
std::optional<analysis::OpId> EvaluationProducer(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, analysis::UseId use) {
  if (const auto assignment = AssignmentProducer(plan, use)) return assignment;
  const auto operand = ResolveEvaluationOperand(plan, graph.uses[use.value].operand);
  const auto* value = std::get_if<analysis::ValueId>(&operand);
  const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
  return result ? std::optional{result->op} : std::nullopt;
}
void BuildAssignmentValues(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  bool needed = false;
  for (const auto& op : graph.ops) {
    budget.Step(); const auto shape = DescribeEvaluation(op.operation);
    needed |= op.life == ValueLife::kLive && shape && shape->assignment_result.has_value();
  }
  if (!needed) return;
  const auto counts = Census(graph, plan, budget);
  for (const auto& item : counts) {
    budget.Step(); if (item.count != kStoreAndArgumentUses || !item.store || !item.argument) continue;
    const auto store = std::get<OpId>(graph.uses[item.store->value].owner);
    const auto call = std::get<OpId>(graph.uses[item.argument->value].owner);
    if (store.value >= call.value) continue;  // Dependency order, not CFG execution proof.
    if (plan.assignment_values.empty()) {
      budget.Array(plan.assignment_values, graph.uses.size());
      budget.Array(plan.assignment_consumers, graph.ops.size());
    }
    plan.assignment_values[item.argument->value] = store;
    plan.assignment_consumers[store.value] = item.argument;
  }
}
void VerifyAssignmentValues(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  if (plan.assignment_values.empty()) { Require(plan.assignment_consumers.empty()); return; }
  Require(plan.assignment_values.size() == graph.uses.size() && plan.assignment_consumers.size() == graph.ops.size());
  const auto counts = Census(graph, plan, budget);
  std::vector<std::uint8_t> seen; budget.Array(seen, graph.ops.size());
  for (std::size_t u = 0; u < plan.assignment_values.size(); ++u) {
    budget.Step(); if (!plan.assignment_values[u]) continue;
    const auto store = *plan.assignment_values[u]; Require(store.value < graph.ops.size() && !seen[store.value]++);
    const auto input = ResolveEvaluationOperand(plan, graph.uses[u].operand);
    const auto* value = std::get_if<ValueId>(&input); Require(value && value->value < counts.size());
    const auto& item = counts[value->value];
    Require(item.count == kStoreAndArgumentUses && item.store && item.argument == UseId{static_cast<std::uint32_t>(u)} &&
        graph.uses[item.store->value].owner == decltype(OperandUse::owner){store});
    const auto& placement = plan.operations[store.value];
    Require(plan.assignment_consumers[store.value] == item.argument && placement.kind == EvaluationKind::kInline &&
        placement.consumer == item.argument && !placement.temporary);
    // The common schedule verifier must independently prove execution, effect
    // suffix, key/RHS boundary and (for phased IR) successful Commit/catch state.
  }
  for (std::size_t i = 0; i < seen.size(); ++i) {
    budget.Step(); Require(bool(seen[i]) == plan.assignment_consumers[i].has_value());
  }
}
}  // namespace abcd::source
