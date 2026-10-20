#include "literal_field_forest.h"

#include "assignment_values.h"
#include "call_plan.h"
#include "effect_order.h"
#include "evaluation_aliases.h"
#include "literal_event_view.h"
#include "logical_evaluation.h"
#include "logical_regions.h"
#include "phase_corridor.h"
#include "phase_predicate_prefix.h"

namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool ok) {
  if (!ok) throw EvaluationError::kOwnership;
}
// The source forest is functional. Inline operands, native selected arms and
// verified guards retain their original parents; a staged definition itself
// stays a boundary until the complete field interval has been checked.
std::vector<OpId> Roots(const ValueProgram& graph, const EvaluationPlan& plan,
                        const LiteralFieldCandidates& candidates,
                        std::span<const PhaseSelectionOperand> bindings,
                        TreeCheckBudget& budget) {
  std::vector<std::optional<OpId>> parent;
  std::vector<OpId> roots, stack;
  std::vector<std::uint8_t> state;
  budget.Array(parent, graph.ops.size());
  budget.Array(roots, graph.ops.size());
  budget.Array(stack, graph.ops.size());
  budget.Array(state, graph.ops.size());
  std::vector<std::optional<OpId>> phi_owners;
  budget.Array(phi_owners, graph.values.size());
  for (std::size_t i = 0; i < plan.logical_values.size(); ++i) {
    budget.Step();
    phi_owners[i] = plan.logical_values[i];
  }
  for (const auto& binding : bindings) {
    budget.Step();
    phi_owners[binding.value.value] = binding.branch;
  }
  const auto owner = [&](UseId use) -> std::optional<OpId> {
    const auto& o = graph.uses[use.value].owner;
    if (const auto* id = std::get_if<OpId>(&o)) return *id;
    const auto v = std::get<ValueId>(o);
    return phi_owners[v.value];
  };
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    const auto& p = plan.operations[i];
    if ((p.kind == EvaluationKind::kInline ||
         p.kind == EvaluationKind::kNestedLiteral ||
         p.kind == EvaluationKind::kPhiInput ||
         p.kind == EvaluationKind::kLogicalValue) &&
        p.consumer)
      parent[i] = owner(*p.consumer);
    if (i < plan.phase_prefixes.size() && plan.phase_prefixes[i])
      parent[i] = plan.phase_prefixes[i];
    if (i < plan.phase_children.size() && plan.phase_children[i])
      parent[i] = plan.phase_children[i];
  }
  for (const auto& binding : bindings) {
    budget.Step();
    if (const auto* arm = std::get_if<PhaseSelectionArm>(&binding.owner))
      parent[binding.branch.value] = arm->parent;
    for (const auto prefix : binding.prefix) {
      budget.Step();
      parent[prefix.value] = binding.branch;
    }
  }
  if (plan.literals) {
    const auto literals = BuildLiteralEventView(graph, *plan.literals, budget);
    for (std::size_t i = 0; i < literals.events.size(); ++i) {
      budget.Step();
      if (literals.events[i] && literals.events[i]->allocation.value != i)
        parent[i] = literals.events[i]->allocation;
    }
  }
  for (const auto& module : plan.modules) {
    budget.Step();
    if (module.pair.guard) parent[module.pair.guard->value] = module.pair.read;
  }
  for (const auto& read : plan.guarded_reads) {
    budget.Step();
    parent[read.guard.value] = read.read;
  }
  for (const auto& object : candidates.objects) {
    budget.Step();
    parent[object.allocation.value].reset();
    parent[object.callee.value].reset();
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (state[i]) continue;
    std::size_t at = i, count = 0;
    while (!state[at]) {
      budget.Step();
      state[at] = 1;
      stack[count++] = {static_cast<std::uint32_t>(at)};
      if (!parent[at]) {
        roots[at] = {static_cast<std::uint32_t>(at)};
        break;
      }
      Require(parent[at]->value < graph.ops.size());
      at = parent[at]->value;
    }
    Require(!parent[at] || state[at] == 2);
    const auto root = roots[at];
    while (count) {
      budget.Step();
      const auto id = stack[--count];
      roots[id.value] = root;
      state[id.value] = 2;
    }
  }
  return roots;
}
LiteralFields Derive(const ValueProgram& graph, const EvaluationPlan& plan,
                     TreeCheckBudget& budget) {
  LiteralFields result;
  const auto candidates = IndexLiteralFieldCandidates(graph, plan, budget);
  if (candidates.objects.empty()) return result;
  const auto regions = FindPhasedPhiSelections(graph, plan, budget);
  const auto bindings =
      FindPhaseSelectionBindings(graph, plan, regions, budget);
  const auto roots = Roots(graph, plan, candidates, bindings, budget);
  std::vector<std::uint8_t> native_selections;
  budget.Array(native_selections, graph.ops.size());
  for (const auto& binding : bindings) {
    budget.Step();
    if (std::holds_alternative<PhaseSelectionBinding>(binding.owner))
      native_selections[binding.branch.value] = 1;
  }
  std::vector<ExecutionAnchor> anchors;
  std::vector<std::optional<OpId>> definitions, root_fields;
  std::vector<std::uint8_t> invalid;
  budget.Array(anchors, graph.ops.size());
  budget.Array(definitions, graph.ops.size());
  budget.Array(root_fields, graph.ops.size());
  budget.Array(invalid, graph.ops.size());
  budget.Array(result.fields, graph.ops.size());
  budget.Array(result.prefix_fields, graph.ops.size());
  budget.Array(result.members, graph.ops.size());
  for (const auto& object : plan.literals->objects) {
    budget.Step();
    if (!candidates.executions[object.allocation.value]) continue;
    anchors[object.allocation.value] = ExecutionAnchor::kRequired;
    for (std::size_t i = 0; i < object.count; ++i) {
      budget.Step();
      const auto id = plan.literals->definitions[object.first + i];
      definitions[id.value] = object.allocation;
      anchors[id.value] = ExecutionAnchor::kRequired;
    }
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    const auto& p = plan.operations[i];
    if (roots[i].value == i && p.kind == EvaluationKind::kRoot &&
        graph.ops[i].life == ValueLife::kLive &&
        (p.temporary || native_selections[i]))
      anchors[i] = ExecutionAnchor::kRequired;
  }
  auto built = BuildEffectOrder(graph, budget.Remaining(), roots, anchors);
  if (const auto* e = std::get_if<ValueAvailabilityError>(&built)) {
    if (*e == ValueAvailabilityError::kResourceLimit)
      throw EvaluationError::kResourceLimit;
    if (*e == ValueAvailabilityError::kWorkLimit)
      throw EvaluationError::kWorkLimit;
    throw EvaluationError::kOwnership;
  }
  const auto& order = std::get<EffectOrder>(built);
  budget.Admit(order.allocated_bytes, 1);
  budget.Step(order.work);
  PhaseBudget phase_budget(budget.Remaining());
  PhaseCorridors phases(graph, plan, phase_budget);
  const auto phase_initial = phase_budget.Cost();
  budget.Admit(phase_initial.bytes, 1);
  budget.Step(phase_initial.work);
  budget.Admit(graph.ops.size(), sizeof(OpId));
  result.prefixes.reserve(graph.ops.size());
  budget.Admit(result.prefixes.capacity() - graph.ops.size(), sizeof(OpId));
  budget.Admit(candidates.objects.size(), sizeof(LiteralFieldObject));
  result.objects.reserve(candidates.objects.size());
  budget.Admit(result.objects.capacity() - candidates.objects.size(),
               sizeof(LiteralFieldObject));
  std::vector<std::optional<OpId>> taken;
  std::vector<std::optional<std::size_t>> object_indices;
  budget.Array(taken, graph.ops.size());
  budget.Array(object_indices, graph.ops.size());
  for (std::size_t i = 0; i < plan.literals->objects.size(); ++i) {
    budget.Step();
    object_indices[plan.literals->objects[i].allocation.value] = i;
  }
  // Each ordered root is consumed at most once across candidates. Overlapping
  // unresolved constructions acquire no permission; no per-object CFG walk.
  for (const auto& candidate : candidates.objects) {
    budget.Step();
    const auto& allocation = graph.ops[candidate.allocation.value];
    const auto& call = graph.ops[candidate.call.value];
    if (order.after[candidate.callee.value] != candidate.allocation ||
        order.before[candidate.allocation.value] !=
            EffectPredecessor{candidate.callee} ||
        !order.Before(graph, candidate.allocation, call.block, call.order))
      continue;
    Require(object_indices[candidate.allocation.value].has_value());
    const auto* object =
        &plan.literals->objects[*object_indices[candidate.allocation.value]];
    const auto start = result.prefixes.size();
    bool complete = true;
    auto previous = candidate.allocation;
    for (std::size_t p = 0; p < object->count && complete; ++p) {
      budget.Step();
      const auto definition = plan.literals->definitions[object->first + p];
      const auto first = result.prefixes.size();
      auto next = order.after[previous.value];
      while (next && *next != definition) {
        budget.Step();
        const auto id = *next;
        const auto& op = graph.ops[id.value];
        const auto shape = DescribeEvaluation(op.operation);
        if (taken[id.value] || definitions[id.value] ||
            candidates.executions[id.value] || roots[id.value] != id ||
            plan.operations[id.value].kind != EvaluationKind::kRoot ||
            (!native_selections[id.value] &&
             (!shape || !shape->expression || !op.result ||
              !plan.operations[id.value].temporary)) ||
            order.before[id.value] != EffectPredecessor{previous} ||
            !order.Before(graph, id, graph.ops[definition.value].block,
                          graph.ops[definition.value].order)) {
          complete = false;
          break;
        }
        taken[id.value] = candidate.allocation;
        result.prefixes.push_back(id);
        root_fields[id.value] = definition;
        previous = id;
        next = order.after[id.value];
      }
      if (!complete || !next ||
          order.before[definition.value] != EffectPredecessor{previous}) {
        complete = false;
        break;
      }
      result.fields[definition.value] = {first, result.prefixes.size() - first};
      root_fields[definition.value] = definition;
      previous = definition;
    }
    if (complete && previous == candidate.last &&
        order.after[previous.value] == candidate.call &&
        order.before[candidate.call.value] == EffectPredecessor{previous} &&
        !phases.HasCatchCopies(candidate.allocation) &&
        (allocation.exceptional != ExceptionalCompletion::kMayThrow ||
         phases.SameCatch(candidate.allocation, candidate.call))) {
      result.objects.push_back(candidate);
    } else {
      invalid[candidate.allocation.value] = 1;
      result.prefixes.resize(start);
    }
    (void)allocation;
  }
  std::vector<std::optional<OpId>> field_allocations;
  budget.Array(field_allocations, graph.ops.size());
  for (const auto& object : result.objects) {
    budget.Step();
    field_allocations[object.allocation.value] = object.allocation;
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    const auto field = root_fields[roots[i].value];
    if (!field) continue;
    const auto allocation = definitions[field->value];
    Require(allocation.has_value());
    if (!field_allocations[allocation->value]) continue;
    result.members[i] = allocation;
    const OpId id{static_cast<std::uint32_t>(i)};
    if (HasOrderedEffect(graph.ops[i]) &&
        (phases.HasCatchCopies(id) ||
         (graph.ops[i].exceptional == ExceptionalCompletion::kMayThrow &&
          !phases.SameCatch(id, *candidates.executions[allocation->value]))))
      invalid[allocation->value] = 1;
  }
  // Membership cannot hide an ordinary operand-tree mutation. Recheck each
  // inline subtree's original effect interval before its forest is atomic.
  // Registered nested compositions keep their independent verifier; they may
  // not use the enclosing literal as their own proof (candidate calls
  // excluded).
  const auto ordinary_roots = IndexPhaseInlineRoots(graph, plan, budget);
  std::vector<std::uint8_t> checked_roots, consumed;
  std::vector<PhaseEffectInterval> intervals;
  budget.Array(checked_roots, graph.ops.size());
  budget.Array(consumed, graph.ops.size());
  budget.Array(intervals, graph.ops.size());
  for (const auto id : plan.phase_compositions) {
    budget.Step();
    Require(id.value < checked_roots.size());
    checked_roots[id.value] = 1;
  }
  for (const auto id : plan.checked_compositions) {
    budget.Step();
    Require(id.value < checked_roots.size());
    checked_roots[id.value] = 1;
  }
  for (const auto& candidate : candidates.objects) {
    budget.Step();
    checked_roots[candidate.call.value] = 0;
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    const OpId id{static_cast<std::uint32_t>(i)};
    intervals[i] = phases.Interval(id);
    if (!result.members[i] || checked_roots[ordinary_roots[i].value]) continue;
    const auto allocation = *result.members[i];
    const auto& op = graph.ops[i];
    const auto kind = plan.operations[i].kind;
    if (op.life != ValueLife::kLive || kind == EvaluationKind::kModuleGuard ||
        kind == EvaluationKind::kCellGuard ||
        kind == EvaluationKind::kGuardedCellRead ||
        kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure)
      continue;
    const auto shape = DescribeEvaluation(op.operation);
    if (!shape || shape->Count() != op.use_count) {
      invalid[allocation.value] = 1;
      continue;
    }
    std::optional<PhaseEffectInterval> children;
    for (std::uint32_t n = 0; n < op.use_count; ++n) {
      budget.Step();
      const UseId use{op.first_use.value + n};
      const auto& occurrence = graph.uses[use.value];
      if (IsMemberReceiver(graph, plan, occurrence) ||
          (definitions[i] && occurrence.role == OperandRole::kBase))
        continue;
      const auto child = EvaluationProducer(graph, plan, use);
      if (!child) continue;
      const auto& placement = plan.operations[child->value];
      if (placement.kind != EvaluationKind::kInline ||
          CrossLogicalInline(graph, plan, *child))
        continue;
      if (placement.consumer != use || consumed[child->value] ||
          shape->NeedsSnapshot(occurrence.role) ||
          occurrence.role == OperandRole::kReceiver ||
          PredicateSnapshot(graph, op, occurrence.operand) ||
          !phases.CanFuse(*child, id, use)) {
        invalid[allocation.value] = 1;
        continue;
      }
      consumed[child->value] = 1;
      const auto interval = intervals[child->value];
      if (interval.count > interval.end ||
          (std::holds_alternative<ValueBranch>(op.operation) && interval.count))
        invalid[allocation.value] = 1;
      if (!interval.count) continue;
      if (children) {
        if (children->end != interval.end - interval.count)
          invalid[allocation.value] = 1;
        children->count += interval.count;
        children->end = interval.end;
      } else
        children = interval;
    }
    if (children) {
      if (children->end != phases.EffectsBefore(id))
        invalid[allocation.value] = 1;
      intervals[i].count += children->count;
    }
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (result.members[i] && !checked_roots[ordinary_roots[i].value] &&
        plan.operations[i].kind == EvaluationKind::kInline && !consumed[i] &&
        !CrossLogicalInline(graph, plan, {static_cast<std::uint32_t>(i)}))
      invalid[result.members[i]->value] = 1;
  }
  budget.Step(phase_budget.Cost().work - phase_initial.work);
  // A retained root is explicitly assigned inside its field, not replayed at
  // any observer. Original SSA dominance and common binding/catch checks still
  // govern every ordinary use and every later field/handler observation.
  std::size_t out = 0;
  for (const auto& object : result.objects) {
    budget.Step();
    if (!invalid[object.allocation.value]) result.objects[out++] = object;
  }
  result.objects.resize(out);
  for (std::size_t i = 0; i < result.members.size(); ++i) {
    budget.Step();
    if (result.members[i] && invalid[result.members[i]->value])
      result.members[i].reset();
  }
  for (auto& field : result.fields) {
    budget.Step();
    field = {};
  }
  std::size_t prefix_out = 0;
  for (std::size_t p = 0; p < result.prefixes.size(); ++p) {
    budget.Step();
    const auto id = result.prefixes[p];
    const auto field = root_fields[id.value];
    if (!field || !result.members[id.value]) continue;
    auto& range = result.fields[field->value];
    if (!range.count) range.first = prefix_out;
    ++range.count;
    result.prefixes[prefix_out++] = id;
    result.prefix_fields[id.value] = field;
  }
  result.prefixes.resize(prefix_out);
  for (const auto& object : result.objects) {
    budget.Step();
    result.members[object.allocation.value] = object.allocation;
  }
  if (result.objects.empty()) return {};
  return result;
}
}  // namespace
LiteralFieldCandidates IndexLiteralFieldCandidates(const ValueProgram& graph,
                                                   const EvaluationPlan& plan,
                                                   TreeCheckBudget& budget) {
  LiteralFieldCandidates result;
  if (!plan.literals || plan.flow != EvaluationFlow::kPhased) return result;
  if (plan.literal_fields.objects.empty()) {
    bool unmet = false;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step();
      const auto property =
          MemberProperty(graph, plan, OpId{static_cast<std::uint32_t>(i)});
      if (property) {
        const auto& saved = plan.operations[property->value];
        unmet |= saved.kind == EvaluationKind::kRoot &&
                 saved.temporary == TemporaryReason::kCrossRegionValue;
      }
    }
    if (!unmet) return result;
  }

  std::vector<std::optional<std::size_t>> allocations;
  std::vector<std::optional<UseId>> external;
  std::vector<std::uint8_t> invalid;
  std::vector<std::optional<OpId>> represented;
  budget.Array(allocations, graph.ops.size());
  budget.Array(external, graph.ops.size());
  budget.Array(invalid, graph.ops.size());
  budget.Array(represented, graph.ops.size());
  for (const auto& existing : plan.literal_fields.objects) {
    budget.Step();
    Require(existing.allocation.value < represented.size() &&
            !represented[existing.allocation.value]);
    represented[existing.allocation.value] = existing.call;
  }
  for (std::size_t i = 0; i < plan.literals->objects.size(); ++i) {
    budget.Step();
    const auto& o = plan.literals->objects[i];
    if (o.form == LiteralForm::kStatements && o.count)
      allocations[o.allocation.value] = i;
  }
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    budget.Step();
    const auto& use = graph.uses[i];
    if (use.life != ValueLife::kLive || IsIdentityPhiInput(plan, use)) continue;
    const auto* owner = std::get_if<OpId>(&use.owner);
    if (owner &&
        (plan.operations[owner->value].kind == EvaluationKind::kAlias ||
         plan.operations[owner->value].kind == EvaluationKind::kElidedPure))
      continue;
    const auto operand = ResolveEvaluationOperand(plan, use.operand);
    const auto* v = std::get_if<ValueId>(&operand);
    const auto* origin =
        v ? std::get_if<OpResult>(&graph.values[v->value].origin) : nullptr;
    if (!origin || !allocations[origin->op.value]) continue;
    if (owner && use.role == OperandRole::kBase &&
        IsStagedLiteral(&*plan.literals, *owner))
      continue;
    if (external[origin->op.value]) invalid[origin->op.value] = 1;
    external[origin->op.value] = UseId{static_cast<std::uint32_t>(i)};
  }
  budget.Array(result.executions, graph.ops.size());
  budget.Admit(plan.literals->objects.size(), sizeof(LiteralFieldObject));
  result.objects.reserve(plan.literals->objects.size());
  budget.Admit(result.objects.capacity() - plan.literals->objects.size(),
               sizeof(LiteralFieldObject));
  for (const auto& object : plan.literals->objects) {
    budget.Step();
    const auto a = object.allocation;
    if (!allocations[a.value] || invalid[a.value] || !external[a.value])
      continue;
    const auto use = *external[a.value];
    const auto& occurrence = graph.uses[use.value];
    const auto* call = std::get_if<OpId>(&occurrence.owner);
    if (!call || occurrence.role != OperandRole::kArgument) continue;
    const auto callee = MemberProperty(graph, plan, *call);
    if (!callee) continue;
    const auto saved = plan.operations[callee->value];
    const bool proposed =
        represented[a.value] == *call &&
        saved.kind == EvaluationKind::kInline &&
        saved.consumer == graph.ops[call->value].first_use &&
        std::binary_search(plan.phase_compositions.begin(),
                           plan.phase_compositions.end(), *call);
    if (!proposed && (saved.kind != EvaluationKind::kRoot ||
                      saved.temporary != TemporaryReason::kCrossRegionValue))
      continue;
    const auto last =
        plan.literals->definitions[object.first + object.count - 1];
    result.objects.push_back({a, last, *call, *callee, use});
    result.executions[a.value] = *call;
    for (std::size_t i = 0; i < object.count; ++i) {
      budget.Step();
      result.executions[plan.literals->definitions[object.first + i].value] =
          *call;
    }
  }
  return result;
}
void BuildLiteralFieldForests(const ValueProgram& graph, EvaluationPlan& plan,
                              TreeCheckBudget& budget) {
  if (!plan.literal_fields.objects.empty()) return;
  plan.literal_fields = Derive(graph, plan, budget);
}
void VerifyLiteralFieldForests(const ValueProgram& graph,
                               const EvaluationPlan& plan,
                               TreeCheckBudget& budget) {
  if (plan.literal_fields.objects.empty()) {
    Require(plan.literal_fields.fields.empty() &&
            plan.literal_fields.prefixes.empty() &&
            plan.literal_fields.prefix_fields.empty() &&
            plan.literal_fields.members.empty());
    return;
  }
  const auto expected = Derive(graph, plan, budget);
  Require(expected == plan.literal_fields);
}
}  // namespace abcd::source
