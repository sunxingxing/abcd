#include "logical_evaluation.h"
#include "cell_evaluation.h"

namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool value) { if (!value) throw EvaluationError::kOwnership; }
struct Interval { std::size_t count = 0, end = 0; };
// Enumerate original eager occurrences in the actual inline expression forest.
// Instruction order alone is insufficient: a later call argument can contain
// an earlier instruction, while an earlier argument reads its retained input.
// Each operation/use is visited once. The same index is rebuilt by verification.
std::vector<UseId> OrderedUses(const ValueProgram& graph, const EvaluationPlan& plan,
    TreeCheckBudget& budget, bool projected = false) {
  struct Frame { OpId operation; std::uint32_t next = 0; };
  enum class Visit : std::uint8_t { kUnseen, kActive, kDone };
  std::vector<Frame> stack; std::vector<Visit> visits; std::vector<UseId> ordered;
  budget.Array(stack, graph.ops.size()); budget.Array(visits, graph.ops.size());
  budget.Array(ordered, graph.uses.size());
  const auto placement_at = [&](std::size_t i) {
    return projected ? plan.operations[i] : BeforeLogicalFusion(graph, plan, i);
  };
  const auto owned = [](EvaluationKind kind) {
    return kind == EvaluationKind::kInline || kind == EvaluationKind::kPredicateSnapshot;
  };
  std::size_t used = 0;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (graph.ops[i].life != ValueLife::kLive ||
        owned(placement_at(i).kind)) continue;
    Require(visits[i] == Visit::kUnseen);
    std::size_t depth = 1; stack[0] = {OpId{static_cast<std::uint32_t>(i)}, 0}; visits[i] = Visit::kActive;
    while (depth) {
      budget.Step(); auto& frame = stack[depth - 1]; const auto& op = graph.ops[frame.operation.value];
      if (frame.next == op.use_count) { visits[frame.operation.value] = Visit::kDone; --depth; continue; }
      const UseId use{op.first_use.value + frame.next}; Require(use.value < graph.uses.size());
      const auto& occurrence = graph.uses[use.value];
      const auto* value = std::get_if<ValueId>(&occurrence.operand);
      const auto* source = value ? std::get_if<OpResult>(&graph.values[ResolveEvaluationValue(plan, *value).value].origin) : nullptr;
      if (source) {
        const auto placement = placement_at(source->op.value);
        if (owned(placement.kind) && placement.consumer == use && visits[source->op.value] != Visit::kDone) {
          Require(visits[source->op.value] == Visit::kUnseen && depth < stack.size());
          visits[source->op.value] = Visit::kActive; stack[depth++] = {source->op, 0}; continue;
        }
      }
      Require(used < ordered.size()); ordered[used++] = use; ++frame.next;
    }
  }
  ordered.resize(used); return ordered;
}
std::vector<std::optional<UseId>> Candidates(const ValueProgram& graph,
    const EvaluationPlan& plan, TreeCheckBudget& budget) {
  std::vector<std::optional<UseId>> first;
  budget.Array(first, graph.ops.size());
  // The first actual eager occurrence acquires initialization; later reads
  // retain its binding. Cross-block ownership remains the documented legacy
  // fast-path gap; this change does not expand that admission condition.
  const auto ordered = OrderedUses(graph, plan, budget);
  for (const auto occurrence : ordered) {
    budget.Step(); const auto& use = graph.uses[occurrence.value];
    if (use.life != ValueLife::kLive) continue;
    const auto* owner = std::get_if<OpId>(&use.owner);
    const auto* value = std::get_if<ValueId>(&use.operand);
    const auto* source = value ? std::get_if<OpResult>(&graph.values[ResolveEvaluationValue(plan, *value).value].origin) : nullptr;
    if (!owner || !source || graph.ops[source->op.value].block != graph.ops[owner->value].block) continue;
    if (plan.operations[owner->value].kind == EvaluationKind::kAlias ||
        (plan.cells && IsInitializedCellGuard(graph, *plan.cells, *owner))) continue;
    const auto placement = BeforeLogicalFusion(graph, plan, source->op.value);
    if (placement.kind != EvaluationKind::kRoot || placement.temporary != TemporaryReason::kMultipleUses) continue;
    auto& previous = first[source->op.value];
    if (!previous) previous = occurrence;
  }
  for (std::size_t i = 0; i < first.size(); ++i) {
    budget.Step(); if (!first[i]) continue;
    const auto& operation = graph.ops[i].operation;
    // Snapshot an expression's value, not an opcode family. The common shape
    // specifies evaluation capability; the forward interval proof below owns
    // every effect and rejects movement across unrelated effects.
    const auto shape = DescribeEvaluation(operation);
    if (!shape || !shape->expression || shape->inputs != InputEvaluation::kEager) { first[i].reset(); continue; }
    const auto owner = std::get<OpId>(graph.uses[first[i]->value].owner);
    const auto& consumer = graph.ops[owner.value];
    const auto consumer_shape = DescribeEvaluation(consumer.operation);
    const auto placement = BeforeLogicalFusion(graph, plan, owner.value);
    // The exact eager occurrence receives one assignment, irrespective of its
    // source syntax. Reference-before-RHS and repeated-input lowering retain
    // their existing snapshots; the interval proof owns all intervening effects.
    const bool eligible = consumer_shape && consumer_shape->inputs == InputEvaluation::kEager &&
        !consumer_shape->NeedsSnapshot(graph.uses[first[i]->value].role) &&
        !PredicateSnapshot(graph, consumer, graph.uses[first[i]->value].operand) &&
        (placement.kind == EvaluationKind::kRoot || placement.kind == EvaluationKind::kInline);
    if (!eligible) first[i].reset();
  }
  return first;
}
std::vector<OpId> UninitializedSnapshots(const ValueProgram& graph,
    const EvaluationPlan& plan, TreeCheckBudget& budget) {
  std::vector<std::optional<UseId>> first; budget.Array(first, graph.ops.size());
  const auto ordered = OrderedUses(graph, plan, budget, true);
  for (const auto use : ordered) {
    budget.Step(); const auto& occurrence = graph.uses[use.value];
    const auto* owner = std::get_if<OpId>(&occurrence.owner);
    if (occurrence.life != ValueLife::kLive || !owner ||
        plan.operations[owner->value].kind == EvaluationKind::kAlias ||
        (plan.cells && IsInitializedCellGuard(graph, *plan.cells, *owner))) continue;
    const auto* value = std::get_if<ValueId>(&occurrence.operand);
    const auto* source = value ? std::get_if<OpResult>(&graph.values[ResolveEvaluationValue(plan, *value).value].origin) : nullptr;
    if (source && plan.operations[source->op.value].kind == EvaluationKind::kPredicateSnapshot && !first[source->op.value])
      first[source->op.value] = use;
  }
  std::vector<OpId> rejected; budget.Array(rejected, graph.ops.size()); std::size_t count = 0;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const auto placement = plan.operations[i];
    if (placement.kind == EvaluationKind::kPredicateSnapshot && first[i] != placement.consumer)
      rejected[count++] = OpId{static_cast<std::uint32_t>(i)};
  }
  rejected.resize(count); return rejected;
}
void RetainSnapshotInputs(const ValueProgram& graph, EvaluationPlan& plan,
    std::span<const OpId> rejected, TreeCheckBudget& budget) {
  // Restoring a definition can execute its inputs earlier. Retain every
  // affected assignment dependency at its original point as well, using one
  // bounded reverse ownership walk rather than repeated whole-forest retries.
  std::vector<OpId> queue; std::vector<std::uint8_t> queued;
  budget.Array(queue, graph.ops.size()); budget.Array(queued, graph.ops.size()); std::size_t size = 0;
  const auto enqueue = [&](OpId id) {
    if (queued[id.value]) return;
    Require(size < queue.size()); queued[id.value] = 1; queue[size++] = id;
  };
  for (const auto id : rejected) {
    budget.Step(); plan.operations[id.value] = {EvaluationKind::kRoot, {}, TemporaryReason::kMultipleUses}; enqueue(id);
  }
  for (std::size_t head = 0; head < size; ++head) {
    budget.Step(); const auto& op = graph.ops[queue[head].value];
    for (std::uint32_t i = 0; i < op.use_count; ++i) {
      budget.Step(); const UseId use{op.first_use.value + i};
      const auto* value = std::get_if<ValueId>(&graph.uses[use.value].operand);
      const auto* source = value ? std::get_if<OpResult>(&graph.values[ResolveEvaluationValue(plan, *value).value].origin) : nullptr;
      if (!source) continue; auto& placement = plan.operations[source->op.value];
      if (placement.kind == EvaluationKind::kPredicateSnapshot) {
        placement = {EvaluationKind::kRoot, {}, TemporaryReason::kMultipleUses}; enqueue(source->op);
      } else if (placement.kind == EvaluationKind::kInline && placement.consumer == use) enqueue(source->op);
    }
  }
}
std::size_t Events(const ValueOp& op, OpEvaluation placement) {
  // Domain proofs discharge guards and fold literal/class operations before
  // forest planning. Count the same projected events as the main planner;
  // independently verified native definitions are not standalone writes here.
  if (placement.kind == EvaluationKind::kCellGuard || placement.kind == EvaluationKind::kModuleGuard ||
      placement.kind == EvaluationKind::kReceiverGuard || placement.kind == EvaluationKind::kInitializer ||
      placement.kind == EvaluationKind::kNestedLiteral || placement.kind == EvaluationKind::kClassProjection) return 0;
  if (std::holds_alternative<ValueModuleRead>(op.operation)) return 2;
  return OrderedEvaluation(op) || IsEnvironmentState(op.operation);
}
}  // namespace
void BuildPredicateSnapshots(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  const auto candidates = Candidates(graph, plan, budget);
  bool any = false; for (const auto candidate : candidates) { budget.Step(); any |= candidate.has_value(); }
  if (!any) return;
  std::vector<Interval> intervals; budget.Array(intervals, graph.ops.size());
  for (const auto& block : graph.blocks) {
    std::size_t serial = 0;
    for (std::uint32_t j = 0; j < block.op_count; ++j) {
      budget.Step(); const OpId id{block.first_op.value + j}; const auto& op = graph.ops[id.value];
      if (op.life != ValueLife::kLive) continue;
      const auto count = Events(op, plan.operations[id.value]); intervals[id.value] = {count, serial + count};
      auto expected = serial;
      for (auto k = op.use_count; k != 0; --k) {
        budget.Step(); const UseId use{op.first_use.value + k - 1};
        const auto* value = std::get_if<ValueId>(&graph.uses[use.value].operand);
        const auto* source = value ? std::get_if<OpResult>(&graph.values[ResolveEvaluationValue(plan, *value).value].origin) : nullptr;
        if (!source || graph.ops[source->op.value].block != op.block) continue;
        const auto placement = plan.operations[source->op.value];
        const bool owned = placement.kind == EvaluationKind::kInline && placement.consumer == use;
        const bool candidate = candidates[source->op.value] == use;
        if (!owned && !candidate) continue;
        const auto interval = intervals[source->op.value];
        if (interval.count && interval.end != expected) { if (owned) break; continue; }
        Require(interval.count <= expected); expected -= interval.count; intervals[id.value].count += interval.count;
        if (candidate) plan.operations[source->op.value] = {EvaluationKind::kPredicateSnapshot, use, TemporaryReason::kMultipleUses};
      }
      serial += count;
    }
  }
  const auto rejected = UninitializedSnapshots(graph, plan, budget);
  if (!rejected.empty()) RetainSnapshotInputs(graph, plan, rejected, budget);
}
void VerifyPredicateSnapshots(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  bool any = false;
  for (const auto& placement : plan.operations) { budget.Step(); any |= placement.kind == EvaluationKind::kPredicateSnapshot; }
  if (!any) return;
  Require(plan.flow == EvaluationFlow::kNormal);
  Require(UninitializedSnapshots(graph, plan, budget).empty());
  std::vector<std::uint8_t> blocks; budget.Array(blocks, graph.blocks.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (plan.operations[i].kind == EvaluationKind::kPredicateSnapshot) blocks[graph.ops[i].block.value] = 1;
  }
  const auto candidates = Candidates(graph, plan, budget);
  std::vector<Interval> intervals; std::vector<std::uint8_t> consumed, needed;
  budget.Array(intervals, graph.ops.size()); budget.Array(consumed, graph.ops.size());
  budget.Array(needed, graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const auto actual = plan.operations[i];
    if (actual.kind != EvaluationKind::kPredicateSnapshot) continue;
    Require(actual.consumer && candidates[i] == actual.consumer && actual.temporary == TemporaryReason::kMultipleUses);
    needed[std::get<OpId>(graph.uses[actual.consumer->value].owner).value] = 1;
  }
  // This proof owns the selected consumers and their original inline operand
  // trees. Other expressions (e.g. a prior folded literal call) retain their
  // separate local proof. Still count every event in the block, so an unrelated
  // intervening effect cannot disappear from a selected snapshot's interval.
  for (const auto& block : graph.blocks) {
    budget.Step(); if (!blocks[&block - graph.blocks.data()]) continue;
    for (auto j = block.op_count; j != 0; --j) {
      budget.Step(); const auto id = block.first_op.value + j - 1;
      if (!needed[id]) continue;
      const auto& op = graph.ops[id];
      for (std::uint32_t k = 0; k < op.use_count; ++k) {
        budget.Step(); const UseId use{op.first_use.value + k};
        const auto* value = std::get_if<ValueId>(&graph.uses[use.value].operand);
        const auto* source = value ? std::get_if<OpResult>(&graph.values[ResolveEvaluationValue(plan, *value).value].origin) : nullptr;
        if (!source || graph.ops[source->op.value].block != op.block) continue;
        const auto before = BeforeLogicalFusion(graph, plan, source->op.value);
        const auto actual = plan.operations[source->op.value];
        if ((before.kind == EvaluationKind::kInline && before.consumer == use) ||
            (actual.kind == EvaluationKind::kPredicateSnapshot && actual.consumer == use)) {
          Require(graph.ops[source->op.value].order < op.order);
          needed[source->op.value] = 1;
        }
      }
    }
  }
  for (const auto& block : graph.blocks) {
    budget.Step(); if (!blocks[&block - graph.blocks.data()]) continue;
    std::size_t serial = 0;
    for (std::uint32_t j = 0; j < block.op_count; ++j) {
      budget.Step(); const OpId id{block.first_op.value + j}; const auto& op = graph.ops[id.value];
      if (op.life != ValueLife::kLive) continue;
      const auto count = Events(op, BeforeLogicalFusion(graph, plan, id.value));
      intervals[id.value] = {count, serial + count};
      if (!needed[id.value]) { serial += count; continue; }
      std::optional<Interval> children;
      for (std::uint32_t k = 0; k < op.use_count; ++k) {
        budget.Step(); const UseId use{op.first_use.value + k};
        const auto* value = std::get_if<ValueId>(&graph.uses[use.value].operand);
        const auto* source = value ? std::get_if<OpResult>(&graph.values[ResolveEvaluationValue(plan, *value).value].origin) : nullptr;
        if (!source || graph.ops[source->op.value].block != op.block) continue;
        const auto placement = BeforeLogicalFusion(graph, plan, source->op.value);
        const auto actual = plan.operations[source->op.value];
        const bool selected = actual.kind == EvaluationKind::kPredicateSnapshot && actual.consumer == use;
        if (!(placement.kind == EvaluationKind::kInline && placement.consumer == use) && !selected) continue;
        if (selected) {
          Require(candidates[source->op.value] == use && actual.temporary == TemporaryReason::kMultipleUses && !consumed[source->op.value]);
          consumed[source->op.value] = 1;
        }
        const auto interval = intervals[source->op.value]; Require(interval.count <= interval.end);
        if (!interval.count) continue;
        if (children) { Require(children->end == interval.end - interval.count); children->count += interval.count; children->end = interval.end; }
        else children = interval;
      }
      if (children) { Require(children->end == serial); intervals[id.value].count += children->count; }
      serial += count;
    }
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); Require((plan.operations[i].kind == EvaluationKind::kPredicateSnapshot) == (consumed[i] != 0));
  }
}
}  // namespace abcd::source
