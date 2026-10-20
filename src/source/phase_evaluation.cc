#include "phase_evaluation.h"
#include "evaluation_failure.h"
#include "assignment_values.h"

#include <new>
#include <stdexcept>
#include "phase_corridor.h"
#include "checked_read_event.h"
#include "logical_evaluation.h"
#include "phase_composition.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = EvaluationError;
void Require(bool condition, Error error) { if (!condition) throw error; }
using EffectInterval = PhaseEffectInterval;
std::optional<OpId> Producer(const ValueProgram& graph, const EvaluationPlan& plan, UseId use) {
  return EvaluationProducer(graph, plan, use);
}
bool Inputs(const ValueOp& op, const OpEvaluation& placement) {
  const auto shape = DescribeEvaluation(op.operation);
  const bool assignment = placement.kind == EvaluationKind::kInline && shape && shape->assignment_result;
  return op.life == ValueLife::kLive && placement.kind != EvaluationKind::kConcurrentNotification && placement.kind != EvaluationKind::kForOfEvent && placement.kind != EvaluationKind::kForInEvent && placement.kind != EvaluationKind::kAlias &&
      placement.kind != EvaluationKind::kElidedPure &&
      (PhaseCorridors::Expression(op.operation) ||
       ((placement.kind == EvaluationKind::kRoot || assignment) && PhaseCorridors::ConsumesOperands(op.operation)));
}
std::vector<OpId> Roots(const ValueProgram& graph, const EvaluationPlan& plan, PhaseBudget& budget) {
  std::vector<OpId> roots;
  budget.Array(roots, graph.ops.size());
  // Every legal edge points to a later operation. Backward substitution gives
  // final execution owners in O(I), including arbitrarily deep pure wrappers.
  for (std::size_t i = graph.ops.size(); i != 0;) {
    budget.Step(); --i;
    roots[i] = OpId{static_cast<std::uint32_t>(i)};
    const auto& placement = plan.operations[i];
    if (placement.kind != EvaluationKind::kInline || CrossLogicalInline(graph, plan, OpId{static_cast<std::uint32_t>(i)})) continue;
    const auto require_owner = [&](bool ok) { if (!ok) throw EvaluationFailure{Error::kOwnership, EvaluationAt(graph, plan, roots[i], EvaluationObligation::kPhaseRoot)}; };
    require_owner(placement.consumer && placement.consumer->value < graph.uses.size());
    const auto* owner = std::get_if<OpId>(&graph.uses[placement.consumer->value].owner);
    require_owner(owner && owner->value > i && owner->value < roots.size());
    roots[i] = roots[owner->value];
  }
  return roots;
}
struct RetryPartition {
  std::span<const OpId> roots;
  std::span<const std::uint8_t> rejected;
};
void Schedule(const ValueProgram& graph, EvaluationPlan& plan, PhaseCorridors& corridors,
    std::vector<EffectInterval>& intervals, PhaseBudget& budget, const RetryPartition* retry = nullptr) {
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const OpId id{static_cast<std::uint32_t>(i)};
    if (retry && !retry->rejected[retry->roots[i].value]) continue;
    const auto& op = graph.ops[i];
    auto expected = corridors.EffectsBefore(id);
    intervals[i] = corridors.Interval(id);
    if (IsArrayInitialization(plan, id) || IsArrayOperand(plan, id) || !Inputs(op, plan.operations[i])) continue;
    const auto shape = DescribeEvaluation(op.operation);
    Require(shape && shape->Count() == op.use_count, Error::kInvalidGraph);
    // Consume an effect suffix in reverse syntax order. Failed choices keep
    // their original snapshots, never substitute .call or reorder arguments.
    for (std::uint32_t ordinal = op.use_count; ordinal != 0; --ordinal) {
      budget.Step(); const UseId use{op.first_use.value + ordinal - 1};
      const auto& occurrence = graph.uses[use.value];
      const auto producer = Producer(graph, plan, use);
      if (!producer || shape->NeedsSnapshot(occurrence.role) ||
          occurrence.role == OperandRole::kReceiver || PredicateSnapshot(graph, op, occurrence.operand)) continue;
      if (retry && retry->roots[producer->value] != retry->roots[i]) continue;
      const auto child = intervals[producer->value];
      // A resultless consumer is the final execution owner. Its catch contract
      // must already admit an effect subtree; a later ancestor cannot repair
      // that obligation. Retain the existing snapshot when evidence is absent.
      const bool branch = std::holds_alternative<ValueBranch>(op.operation);
      // The first pass proposes a forest, not a publishable schedule. Include
      // a control consumer so rejection can partition the entire owned tree:
      // retain throwing producers, then compose its pure comparison suffix.
      // Otherwise an early getter/comparison choice hides that legal suffix.
      if (!op.result && child.count && (retry || !branch) && !corridors.SameCatch(*producer, id)) continue;
      // Normal control has no throw dispatch. The partitioned final forest
      // still admits only pure children; provisional effects are rejected below.
      if (retry && branch && child.count) continue;
      if (plan.operations[producer->value].kind != EvaluationKind::kRoot ||
          (child.count && child.end != expected) ||
          !corridors.CanFuse(*producer, id, use)) continue;
      // Retry only inside a rejected tree. Every retained effect subtree has
      // its root's catch signature by induction; require the same signature at
      // each new parent. Pure roots lacking dispatch evidence keep snapshots.
      // Single-use producers bound these comparisons by original copy volume.
      if (retry && child.count && EvaluationMayThrow(graph, plan, *producer) &&
          !corridors.SameCatch(*producer, id)) continue;
      Require(child.count <= expected, Error::kInvalidGraph);
      expected -= child.count; intervals[i].count += child.count;
      plan.operations[producer->value] = {EvaluationKind::kInline, use, {}};
    }
  }
}
}  // namespace
bool CrossEvaluationInline(const ValueProgram& graph, const EvaluationPlan& plan, OpId producer) {
  const auto& placement = plan.operations[producer.value];
  const bool closed = plan.flow == EvaluationFlow::kNormal && producer.value < plan.closed_expressions.size() &&
      plan.closed_expressions[producer.value] && plan.closed_expressions[producer.value] == placement.consumer &&
      IsClosedPureValue(graph, producer);
  if ((plan.flow != EvaluationFlow::kPhased && !closed) || CrossLogicalInline(graph, plan, producer) ||
      (placement.kind != EvaluationKind::kInline && placement.kind != EvaluationKind::kNestedLiteral) ||
      !placement.consumer) return false;
  const auto* owner = std::get_if<OpId>(&graph.uses[placement.consumer->value].owner);
  return owner && graph.ops[owner->value].block != graph.ops[producer.value].block;
}
std::variant<PhaseEvaluationCost, EvaluationFailure> BuildPhaseExpressions(
    const ValueProgram& graph, EvaluationPlan& plan, SemanticBudget allowance) {
  try {
    if (plan.flow != EvaluationFlow::kPhased) return PhaseEvaluationCost{};
    PhaseBudget budget(allowance);
    PhaseCorridors corridors(graph, plan, budget);
    std::vector<EffectInterval> intervals;
    std::vector<OpEvaluation> original;
    budget.Array(intervals, graph.ops.size());
    budget.Array(original, graph.ops.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); original[i] = plan.operations[i];
    }
    Schedule(graph, plan, corridors, intervals, budget);
    const auto roots = Roots(graph, plan, budget);
    std::vector<std::uint8_t> rejected;
    budget.Array(rejected, graph.ops.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); const OpId id{static_cast<std::uint32_t>(i)};
      if (plan.operations[i].kind == EvaluationKind::kInline &&
          PhaseCorridors::Event(graph.ops[i], plan.operations[i]) &&
          (std::holds_alternative<ValueBranch>(graph.ops[roots[i].value].operation) ||
           (EvaluationMayThrow(graph, plan, OpId{static_cast<std::uint32_t>(i)}) &&
            !corridors.SameCatch(id, roots[i])))) rejected[roots[i].value] = 1;
    }
    // Restore only rejected trees, then partition them in one constrained pass.
    // Accepted trees remain untouched. No fixed-point retry or ancestor walks;
    // final independent verification still checks every resulting subtree.
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); if (rejected[roots[i].value]) plan.operations[i] = original[i];
    }
    const RetryPartition retry{roots, rejected};
    Schedule(graph, plan, corridors, intervals, budget, &retry);
    return budget.Cost();
  } catch (const EvaluationFailure& error) { return error;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<PhaseEvaluationCost, EvaluationFailure> VerifyPhaseExpressions(
    const ValueProgram& graph, const EvaluationPlan& plan, SemanticBudget allowance) {
  try {
    if (plan.flow != EvaluationFlow::kPhased) return PhaseEvaluationCost{};
    TreeCheckBudget selected_budget(allowance);
    if (!plan.phase_children.empty() || !plan.phase_selections.empty() || !plan.phase_transports.empty() || !plan.phase_prefixes.empty() || !plan.phase_references.empty() || !plan.logical_values.empty() || !plan.phase_compositions.empty())
      VerifyPhaseSelectionOperands(graph, plan, selected_budget);
    // Composed trees are proved whole by their own ordered-event equations;
    // ordinary corridor coordinates are never mixed into the same equation.
    const auto composed = VerifyPhaseCompositions(graph, plan, selected_budget);
    const auto in_composition = [&](std::size_t op) { return !composed.empty() && composed[op]; };
    PhaseBudget budget(selected_budget.Remaining());
    PhaseCorridors corridors(graph, plan, budget);
    const auto roots = Roots(graph, plan, budget);
    std::vector<EffectInterval> intervals;
    std::vector<std::uint8_t> consumed;
    budget.Array(intervals, graph.ops.size()); budget.Array(consumed, graph.ops.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); const OpId id{static_cast<std::uint32_t>(i)};
      const auto& op = graph.ops[i];
      const auto end = corridors.EffectsBefore(id);
      intervals[i] = corridors.Interval(id);
      if (in_composition(i) || MemberUpdateIntrinsic(plan,{static_cast<std::uint32_t>(i)}) || IsArrayInitialization(plan, {static_cast<std::uint32_t>(i)}) ||
          IsArrayOperand(plan, {static_cast<std::uint32_t>(i)}) || !Inputs(op, BeforeLogicalFusion(graph, plan, i))) continue;
      const auto shape = DescribeEvaluation(op.operation);
      Require(shape && shape->Count() == op.use_count, Error::kInvalidGraph);
      std::optional<EffectInterval> children;
      // Independent forward equations: proposed ordered child intervals must
      // concatenate without gaps/overlap, then end immediately before this op.
      for (std::uint32_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
        budget.Step(); const UseId use{op.first_use.value + ordinal};
        const auto producer = Producer(graph, plan, use);
        if (!producer || plan.operations[producer->value].kind != EvaluationKind::kInline ||
            CrossLogicalInline(graph, plan, *producer)) continue;
        const auto& occurrence = graph.uses[use.value];
        if (IsMemberReceiver(graph, plan, occurrence)) continue;
        const auto child = intervals[producer->value];
        Require(!std::holds_alternative<ValueBranch>(op.operation) || child.count == 0, Error::kOrder);
        Require(plan.operations[producer->value].consumer == use && !consumed[producer->value] &&
            !shape->NeedsSnapshot(occurrence.role) && occurrence.role != OperandRole::kReceiver &&
            !PredicateSnapshot(graph, op, occurrence.operand) &&
            corridors.CanFuse(*producer, id, use), Error::kOrder);
        consumed[producer->value] = 1;
        Require(child.count <= child.end, Error::kOrder);
        if (child.count) {
          if (children) {
            Require(children->end == child.end - child.count, Error::kOrder);
            children->count += child.count; children->end = child.end;
          } else children = child;
        }
      }
      if (children) {
        Require(children->end == end, Error::kOrder);
        intervals[i].count += children->count;
      }
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step();
      if (!(plan.operations[i].kind != EvaluationKind::kInline || consumed[i] || in_composition(i) ||
          CrossLogicalInline(graph, plan, OpId{static_cast<std::uint32_t>(i)})))
        throw EvaluationFailure{Error::kOwnership, EvaluationAt(graph, plan, OpId{static_cast<std::uint32_t>(i)}, EvaluationObligation::kPhaseConsumption)};
      if (plan.operations[i].kind == EvaluationKind::kInline && !in_composition(i) &&
          !CrossLogicalInline(graph, plan, OpId{static_cast<std::uint32_t>(i)}) &&
          PhaseCorridors::Event(graph.ops[i], plan.operations[i]) &&
          EvaluationMayThrow(graph, plan, OpId{static_cast<std::uint32_t>(i)}))
        Require(corridors.SameCatch(OpId{static_cast<std::uint32_t>(i)}, roots[i]), Error::kOrder);
    }
    const auto cost = budget.Cost();
    return PhaseEvaluationCost{selected_budget.Bytes() + cost.bytes, selected_budget.Work() + cost.work};
  } catch (TreeStructureError error) {
    return error == TreeStructureError::kWorkLimit ? Error::kWorkLimit : Error::kResourceLimit;
  } catch (const EvaluationFailure& error) { return error;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
