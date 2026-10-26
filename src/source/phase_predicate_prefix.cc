#include "phase_predicate_prefix.h"
#include "phase_corridor.h"
#include "evaluation_aliases.h"
#include "logical_regions.h"
#include <algorithm>
namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool value) { if (!value) throw EvaluationError::kOwnership; }
}
std::vector<OpId> IndexPhaseInlineRoots(const ValueProgram& graph,
    const EvaluationPlan& plan, TreeCheckBudget& budget) {
  std::vector<std::optional<OpId>> parents;
  std::vector<OpId> roots, stack;
  enum class Visit : std::uint8_t { kUnseen, kActive, kDone };
  std::vector<Visit> color;
  budget.Array(parents, graph.ops.size()); budget.Array(roots, graph.ops.size());
  budget.Array(stack, graph.ops.size()); budget.Array(color, graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const auto placement = BeforeLogicalFusion(graph, plan, i);
    if (placement.kind != EvaluationKind::kInline || !placement.consumer) continue;
    Require(placement.consumer->value < graph.uses.size());
    if (const auto* owner = std::get_if<OpId>(&graph.uses[placement.consumer->value].owner)) parents[i] = *owner;
  }
  for (const auto& module : plan.modules) {
    budget.Step(); if (module.pair.guard) parents[module.pair.guard->value] = module.pair.read;
  }
  for (const auto& pair : plan.guarded_reads) {
    budget.Step();
    if (plan.operations[pair.read.value].kind == EvaluationKind::kInline)
      parents[pair.guard.value] = pair.read;
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (color[i] != Visit::kUnseen) continue;
    std::size_t depth = 0, at = i;
    while (color[at] == Visit::kUnseen) {
      budget.Step(); color[at] = Visit::kActive; stack[depth++] = OpId{static_cast<std::uint32_t>(at)};
      if (!parents[at]) { roots[at] = OpId{static_cast<std::uint32_t>(at)}; break; }
      Require(parents[at]->value < graph.ops.size()); at = parents[at]->value;
    }
    Require(!parents[at] || color[at] == Visit::kDone);
    const auto root = roots[at];
    while (depth) { budget.Step(); const auto id = stack[--depth]; roots[id.value] = root; color[id.value] = Visit::kDone; }
  }
  return roots;
}
PhasePredicatePrefixes IndexPhasePredicatePrefixes(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, std::span<const OpId> roots, TreeCheckBudget& budget,
    std::span<const SelectionRegion> regions) {
  Require(roots.size() == graph.ops.size());
  struct Edge { OpId source; std::optional<std::size_t> next; };
  std::vector<std::optional<std::size_t>> heads;
  std::vector<std::optional<OpId>> destinations;
  std::vector<std::size_t> pending;
  std::vector<std::uint8_t> candidates, foreign;
  std::vector<OpId> queue;
  std::vector<Edge> edges;
  struct PhiOwner { OpId branch; ValueId merged; };
  std::vector<std::optional<OpId>> enclosures;
  std::vector<std::optional<PhiOwner>> phi_owners;
  budget.Array(enclosures, graph.ops.size()); budget.Array(phi_owners, graph.edges.size());
  // A selection reads its retained inputs in both its predicates and its
  // selected arms. These are one source enclosure, not competing consumers.
  // This index proposes ownership only: PredicatePrefix must still prove the
  // producer executes before the header, and Scope checks every original use.
  for (const auto& region : regions) {
    budget.Step();
    for (const auto& condition : region.conditions) {
      budget.Step(); enclosures[roots[condition.branch.value].value] = region.branch;
    }
    for (const auto* arm : {&region.right, region.consequent ? &*region.consequent : nullptr}) {
      budget.Step(); if (!arm) continue;
      const auto* value = std::get_if<ValueId>(arm);
      const auto* origin = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
      if (origin) enclosures[roots[origin->op.value].value] = region.branch;
    }
    for (const auto input : {region.keep_input, region.evaluate_input}) {
      budget.Step(); if (input) phi_owners[input->value] = PhiOwner{region.branch, region.merged};
    }
  }
  budget.Array(heads, graph.ops.size()); budget.Array(destinations, graph.ops.size());
  budget.Array(pending, graph.ops.size()); budget.Array(candidates, graph.ops.size());
  budget.Array(foreign, graph.ops.size()); budget.Array(queue, graph.ops.size());
  budget.Array(edges, graph.uses.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (roots[i] != OpId{static_cast<std::uint32_t>(i)}) continue;
    const auto& op = graph.ops[i]; const auto shape = DescribeEvaluation(op.operation);
    candidates[i] = op.life == ValueLife::kLive && op.result && shape && shape->expression &&
        PhaseCorridors::ConsumesOperands(op.operation) &&
        graph.values[op.result->value].domain.IsLanguage() &&
        BeforeLogicalFusion(graph, plan, i).kind == EvaluationKind::kRoot;
  }
  std::size_t edge_count = 0;
  for (const auto& use : graph.uses) {
    budget.Step(); if (use.life != ValueLife::kLive || IsIdentityPhiInput(plan, use)) continue;
    const auto operand = ResolveEvaluationOperand(plan, use.operand);
    const auto* value = std::get_if<ValueId>(&operand);
    const auto* result = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
    if (!result) continue;
    const auto source = roots[result->op.value]; if (!candidates[source.value]) continue;
    std::optional<OpId> target;
    if (const auto* owner = std::get_if<OpId>(&use.owner)) {
      const auto placement = BeforeLogicalFusion(graph, plan, owner->value).kind;
      if (placement == EvaluationKind::kAlias || placement == EvaluationKind::kElidedPure) continue;
      const auto root = roots[owner->value];
      target = enclosures[root.value].value_or(root);
    } else if (use.role == OperandRole::kPhiInput && use.incoming) {
      if (const auto* edge = std::get_if<EdgeId>(&*use.incoming)) {
        const auto& owner = phi_owners[edge->value];
        if (owner && std::get<ValueId>(use.owner) == owner->merged) target = owner->branch;
      }
    }
    if (!target) { foreign[source.value] = 1; continue; }
    if (*target == source) continue;
    // A callee is a Reference acquisition, not a movable value prefix. The
    // existing Reference owner must authorize its receiver/invocation pairing.
    if (use.role == OperandRole::kCallee) foreign[source.value] = 1;
    edges[edge_count] = {source, heads[target->value]}; heads[target->value] = edge_count++;
    ++pending[source.value];
  }
  // Reverse-use Kahn equations: every external consumer must resolve to the
  // same terminal owner. Multiple observations inside that owner are allowed;
  // unresolved cycles and competing owners do not acquire source ownership.
  std::size_t begin = 0, end = 0;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const OpId id{static_cast<std::uint32_t>(i)};
    if (roots[i] != id) continue;
    if (!candidates[i] || foreign[i] || !pending[i]) { destinations[i] = id; queue[end++] = id; }
  }
  while (begin < end) {
    budget.Step(); const auto target = queue[begin++];
    for (auto edge = heads[target.value]; edge; edge = edges[*edge].next) {
      budget.Step(); const auto source = edges[*edge].source;
      if (!candidates[source.value] || foreign[source.value]) continue;
      Require(pending[source.value]);
      auto& destination = destinations[source.value];
      if (!destination) destination = destinations[target.value];
      else if (destination != destinations[target.value]) destination = source;
      if (!--pending[source.value]) queue[end++] = source;
    }
  }
  PhasePredicatePrefixes result;
  budget.Array(result.ranges, graph.ops.size());
  std::vector<ExecutionAnchor> anchors; budget.Array(anchors, graph.ops.size());
  std::vector<std::uint8_t> ordered; budget.Array(ordered, graph.ops.size());
  // This placement represents an ordered condition evaluation, including its
  // pure dependencies. Pure-only materializations keep their existing binding
  // placement; moving them into a sequence adds syntax without owning an effect.
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const auto root = roots[i];
    if (!HasOrderedEffect(graph.ops[i]) || pending[root.value] || !destinations[root.value] ||
        *destinations[root.value] == root) continue;
    ordered[destinations[root.value]->value] = 1;
  }
  std::size_t count = 0;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (!destinations[i] || pending[i] || !candidates[i] || foreign[i] ||
        *destinations[i] == OpId{static_cast<std::uint32_t>(i)}) continue;
    const auto branch = *destinations[i];
    if (!ordered[branch.value] || !std::holds_alternative<ValueBranch>(graph.ops[branch.value].operation)) continue;
    ++result.ranges[branch.value].count; ++count;
    anchors[i] = anchors[branch.value] = ExecutionAnchor::kRequired;
  }
  if (!count) return result;
  auto built = BuildEffectOrder(graph, budget.Remaining(), roots, anchors);
  if (const auto* error = std::get_if<ValueAvailabilityError>(&built)) {
    if (*error == ValueAvailabilityError::kWorkLimit) throw EvaluationError::kWorkLimit;
    if (*error == ValueAvailabilityError::kResourceLimit) throw EvaluationError::kResourceLimit;
    throw EvaluationError::kInvalidGraph;
  }
  result.order = std::get<EffectOrder>(std::move(built));
  budget.Admit(result.order.allocated_bytes, 1); budget.Step(result.order.work);
  budget.Array(result.operations, count);
  std::vector<std::size_t> cursors; budget.Array(cursors, graph.ops.size());
  std::size_t offset = 0;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); result.ranges[i].first = cursors[i] = offset; offset += result.ranges[i].count;
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (anchors[i] != ExecutionAnchor::kRequired || !destinations[i] ||
        *destinations[i] == OpId{static_cast<std::uint32_t>(i)}) continue;
    result.operations[cursors[destinations[i]->value]++] = OpId{static_cast<std::uint32_t>(i)};
  }
  for (const auto range : result.ranges) {
    budget.Step(); auto first = result.operations.begin() + range.first;
    std::sort(first, first + range.count, [&](OpId a, OpId b) {
      budget.Step(); return result.order.position[a.value] < result.order.position[b.value];
    });
  }
  return result;
}
}  // namespace abcd::source
