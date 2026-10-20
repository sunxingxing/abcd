#include "assignment_values.h"
#include "call_plan.h"
#include "effect_order.h"
#include "evaluation_aliases.h"
#include "literal_reference_view.h"
#include "literal_field_forest.h"
#include "logical_evaluation.h"
#include "phase_corridor.h"

namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool value) { if (!value) throw EvaluationError::kOwnership; }

// Source ownership is a functional forest, including selected phi arms and
// native nested initializers. Resolve each parent once; storage/PC order is not
// an ownership proof, and a cycle cannot become an executable source subtree.
std::vector<OpId> Owners(const ValueProgram& graph, const EvaluationPlan& plan,
                         TreeCheckBudget& budget,
                         bool restore_references = false) {
  std::vector<std::optional<OpId>> parents;
  std::vector<OpId> roots, stack;
  std::vector<std::uint8_t> color;
  budget.Array(parents, graph.ops.size()); budget.Array(roots, graph.ops.size());
  budget.Array(stack, graph.ops.size()); budget.Array(color, graph.ops.size());
  const auto consumer = [&](UseId use) -> std::optional<OpId> {
    Require(use.value < graph.uses.size());
    const auto& owner = graph.uses[use.value].owner;
    if (const auto* op = std::get_if<OpId>(&owner)) return *op;
    const auto value = std::get<ValueId>(owner);
    return value.value < plan.logical_values.size() ? plan.logical_values[value.value] : std::nullopt;
  };
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const auto& placement = plan.operations[i];
    // A composed staged allocation is the forest's original boundary, not an
    // owned child of the call when rederiving its allocation/definition chain.
    if (i < plan.logical_objects.size() && plan.logical_objects[i]) continue;
    if ((placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kNestedLiteral ||
         placement.kind == EvaluationKind::kPhiInput || placement.kind == EvaluationKind::kLogicalValue) &&
        placement.consumer) parents[i] = consumer(*placement.consumer);
    if (i < plan.phase_prefixes.size() && plan.phase_prefixes[i]) parents[i] = plan.phase_prefixes[i];
    if (i < plan.phase_children.size() && plan.phase_children[i]) parents[i] = plan.phase_children[i];
  }
  if (restore_references)
    for (const auto call : plan.phase_compositions) {
      budget.Step();
      if (const auto property = MemberProperty(graph, plan, call))
        parents[property->value].reset();
    }
  for (const auto& module : plan.modules) {
    budget.Step(); if (module.pair.guard) parents[module.pair.guard->value] = module.pair.read;
  }
  if (plan.literals) for (const auto& object : plan.literals->objects) {
    budget.Step(); if (object.form != LiteralForm::kExpression) continue;
    for (std::uint32_t p = 0; p < object.count; ++p) {
      budget.Step(); parents[plan.literals->definitions[object.first + p].value] = object.allocation;
    }
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (color[i]) continue;
    std::size_t depth = 0, at = i;
    while (!color[at]) {
      budget.Step(); color[at] = 1; stack[depth++] = OpId{static_cast<std::uint32_t>(at)};
      if (!parents[at]) { roots[at] = OpId{static_cast<std::uint32_t>(at)}; break; }
      Require(parents[at]->value < graph.ops.size()); at = parents[at]->value;
    }
    Require(!parents[at] || color[at] == 2);
    const auto root = roots[at];
    while (depth) { budget.Step(); const auto node = stack[--depth]; roots[node.value] = root; color[node.value] = 2; }
  }
  return roots;
}

std::vector<std::optional<OpId>> Objects(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  std::vector<std::optional<OpId>> selected;
  if (!plan.literals) return selected;
  bool staged = false;
  for (const auto& object : plan.literals->objects) { budget.Step(); staged |= object.form == LiteralForm::kStatements; }
  if (!staged) return selected;
  // Operand ownership is checked separately by the phase expression/selection
  // analyses. The projected effect chain proves allocation/definition count and
  // order; original full-flow dominance still owns leaves and observations.
  const auto roots = Owners(graph, plan, budget);
  auto built = BuildEffectOrder(graph, budget.Remaining(), roots);
  if (const auto* error = std::get_if<ValueAvailabilityError>(&built)) {
    if (*error == ValueAvailabilityError::kResourceLimit) throw EvaluationError::kResourceLimit;
    if (*error == ValueAvailabilityError::kWorkLimit) throw EvaluationError::kWorkLimit;
    throw EvaluationError::kInvalidGraph;
  }
  const auto& effects = std::get<EffectOrder>(built);
  budget.Admit(effects.allocated_bytes, 1); budget.Step(effects.work);
  std::vector<std::optional<OpId>> allocations, catches;
  std::vector<std::uint8_t> rejected;
  budget.Array(allocations, graph.ops.size()); budget.Array(catches, graph.ops.size());
  budget.Array(rejected, graph.ops.size()); budget.Array(selected, graph.ops.size());
  PhaseBudget phase_budget(budget.Remaining());
  PhaseCorridors phases(graph, plan, phase_budget, PhaseLiteralIntervals::kExclude);
  for (const auto& object : plan.literals->objects) {
    budget.Step(); if (object.form != LiteralForm::kStatements) continue;
    if (graph.ops[object.allocation.value].exceptional == ExceptionalCompletion::kMayThrow)
      catches[object.allocation.value] = object.allocation;
    auto previous = object.allocation;
    for (std::uint32_t p = 0; p < object.count; ++p) {
      budget.Step(); const auto id = plan.literals->definitions[object.first + p];
      allocations[id.value] = object.allocation;
      if (effects.after[previous.value] != id || effects.before[id.value] != EffectPredecessor{previous})
        rejected[object.allocation.value] = 1;
      previous = id;
    }
    selected[object.allocation.value] = previous;
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const auto allocation = allocations[roots[i].value];
    if (!allocation || !HasOrderedEffect(graph.ops[i])) continue;
    const auto id = OpId{static_cast<std::uint32_t>(i)};
    if (graph.ops[i].exceptional != ExceptionalCompletion::kMayThrow) continue;
    auto& caught = catches[allocation->value];
    if (!caught) {
      // A nonthrowing creation cannot emit catch staging. This is the same
      // representation obligation as native late-expression initialization.
      if (phases.HasCatchCopies(id)) rejected[allocation->value] = 1;
      caught = id;
    }
    if (!phases.SameCatch(id, *caught)) rejected[allocation->value] = 1;
  }
  const auto root_of = [&](ValueId value) -> std::optional<OpId> {
    const auto& origin = graph.values[value.value].origin;
    if (const auto* result = std::get_if<OpResult>(&origin)) return roots[result->op.value];
    if (value.value < plan.logical_values.size() && plan.logical_values[value.value])
      return roots[plan.logical_values[value.value]->value];
    return {};
  };
  // One use census proves every external source leaf is available before the
  // allocation. A pure late snapshot is still a late binding, not a movable
  // operand. Selected values and checked nested expressions own their leaves.
  for (std::size_t u = 0; u < graph.uses.size(); ++u) {
    budget.Step(); const auto& use = graph.uses[u]; if (use.life != ValueLife::kLive) continue;
    std::optional<OpId> owner;
    if (const auto* op = std::get_if<OpId>(&use.owner)) {
      const auto kind = plan.operations[op->value].kind;
      if (graph.ops[op->value].life != ValueLife::kLive || kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure) continue;
      owner = roots[op->value];
      if (allocations[op->value] && use.role == OperandRole::kBase && u == graph.ops[op->value].first_use.value) continue;
    } else owner = root_of(std::get<ValueId>(use.owner));
    if (!owner || !allocations[owner->value]) continue;
    const auto allocation = *allocations[owner->value];
    const auto operand = ResolveEvaluationOperand(plan, use.operand);
    const auto* value = std::get_if<ValueId>(&operand);
    if (!value) continue;
    const auto& site = graph.ops[allocation.value];
    if (!graph.values[value->value].domain.IsLanguage() ||
        (root_of(*value) != owner && !ValueAvailableAt(graph, effects.dominators, *value, site.block, site.order)))
      rejected[allocation.value] = 1;
  }
  budget.Admit(phase_budget.Cost().bytes, 1); budget.Step(phase_budget.Cost().work);
  for (std::size_t i = 0; i < selected.size(); ++i) { budget.Step(); if (rejected[i]) selected[i].reset(); }
  for(const auto& object:plan.literal_fields.objects){budget.Step();selected[object.allocation.value]=object.last;}
  return selected;
}
}  // namespace

static std::vector<std::optional<LiteralReferenceView>> IndexLegacyLiteralReferences(
    const ValueProgram& graph, const EvaluationPlan& plan,
    TreeCheckBudget& budget, std::vector<std::optional<OpId>>* members) {
  std::vector<std::optional<LiteralReferenceView>> result;
  if (plan.literal_arm_inputs.empty() || plan.logical_objects.empty() || !plan.literals) return result;
  const auto roots = Owners(graph, plan, budget, true);
  auto checked = BuildEffectOrder(graph, budget.Remaining(), roots);
  if (const auto* e = std::get_if<ValueAvailabilityError>(&checked)) {
    if (*e == ValueAvailabilityError::kResourceLimit)
      throw EvaluationError::kResourceLimit;
    if (*e == ValueAvailabilityError::kWorkLimit)
      throw EvaluationError::kWorkLimit;
    throw EvaluationError::kOwnership;
  }
  const auto& effects = std::get<EffectOrder>(checked);
  budget.Admit(effects.allocated_bytes, 1);
  budget.Step(effects.work);
  std::vector<std::optional<OpId>> fields;
  std::vector<std::optional<UseId>> external;
  std::vector<std::uint8_t> invalid, selected;
  budget.Array(fields, graph.ops.size());
  budget.Array(external, graph.ops.size());
  budget.Array(invalid, graph.ops.size());
  budget.Array(selected, graph.ops.size());
  budget.Array(result, graph.ops.size());
  std::vector<std::optional<OpId>> local_members;
  auto& owned = members ? *members : local_members;
  budget.Array(owned, graph.ops.size());
  for (auto& member : owned) {
    budget.Step();
    member.reset();
  }
  for (const auto& object : plan.literals->objects) {
    budget.Step();
    if (!plan.logical_objects[object.allocation.value]) continue;
    for (std::uint32_t i = 0; i < object.count; ++i) {
      budget.Step();
      fields[plan.literals->definitions[object.first + i].value] =
          object.allocation;
    }
  }
  for (const auto& selection : plan.phase_selections) {
    budget.Step();
    if (!selection.input) continue;
    const auto* owner =
        std::get_if<OpId>(&graph.uses[selection.input->value].owner);
    if (owner && fields[owner->value])
      selected[fields[owner->value]->value] = 1;
  }
  for (std::size_t u = 0; u < graph.uses.size(); ++u) {
    budget.Step();
    const auto& use = graph.uses[u];
    if (use.life != ValueLife::kLive || IsIdentityPhiInput(plan, use)) continue;
    const auto* owner = std::get_if<OpId>(&use.owner);
    if (owner &&
        (plan.operations[owner->value].kind == EvaluationKind::kAlias ||
         plan.operations[owner->value].kind == EvaluationKind::kElidedPure))
      continue;
    const auto value = ResolveEvaluationOperand(plan, use.operand);
    const auto* v = std::get_if<ValueId>(&value);
    const auto* origin =
        v ? std::get_if<OpResult>(&graph.values[v->value].origin) : nullptr;
    if (!origin || !selected[origin->op.value]) continue;
    if (owner && fields[owner->value] == origin->op &&
        use.role == OperandRole::kBase)
      continue;
    if (external[origin->op.value]) invalid[origin->op.value] = 1;
    external[origin->op.value] = UseId{static_cast<std::uint32_t>(u)};
  }
  for (const auto& object : plan.literals->objects) {
    budget.Step();
    const auto allocation = object.allocation;
    if (!selected[allocation.value] || invalid[allocation.value] ||
        !external[allocation.value])
      continue;
    const auto use = *external[allocation.value];
    const auto& occurrence = graph.uses[use.value];
    const auto* call = std::get_if<OpId>(&occurrence.owner);
    if (!call || occurrence.role != OperandRole::kArgument) continue;
    const auto callee = MemberProperty(graph, plan, *call);
    if (!callee) continue;
    const auto saved = BeforeLogicalFusion(graph, plan, callee->value);
    const bool proposed =
        saved.kind == EvaluationKind::kInline &&
        saved.consumer == graph.ops[call->value].first_use &&
        std::binary_search(plan.phase_compositions.begin(),
                           plan.phase_compositions.end(), *call);
    if (!proposed && (saved.kind != EvaluationKind::kRoot ||
                      saved.temporary != TemporaryReason::kCrossRegionValue))
      continue;
    const auto last = *plan.logical_objects[allocation.value];
    // The complete object proof contracts selected arms into their owning
    // definition. Original owner edges must leave no unrelated effect between
    // saved Reference, allocation, definitions and invocation.
    auto previous = allocation;
    bool ordered = true;
    for (std::uint32_t i = 0; i < object.count; ++i) {
      budget.Step();
      const auto definition = plan.literals->definitions[object.first + i];
      ordered &=
          effects.after[previous.value] == definition &&
          effects.before[definition.value] == EffectPredecessor{previous};
      previous = definition;
    }
    if (!ordered || previous != last ||
        effects.after[callee->value] != allocation ||
        effects.before[allocation.value] != EffectPredecessor{*callee} ||
        effects.after[last.value] != *call ||
        effects.before[call->value] != EffectPredecessor{last})
      continue;
    result[allocation.value] =
        LiteralReferenceView{allocation, last, *call, *callee, use};
  }
  PhaseBudget phase_budget(budget.Remaining());
  PhaseCorridors phases(graph, plan, phase_budget);
  // One pass charges each original forest event once. Source-owner roots also
  // include predicate prefixes, selected arm literals, nested values and calls.
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    const auto root = roots[i];
    const auto allocation =
        fields[root.value] ? fields[root.value] : std::optional{root};
    if (!allocation || !result[allocation->value]) continue;
    owned[i] = allocation;
    if (!HasOrderedEffect(graph.ops[i])) continue;
    auto& proof = *result[allocation->value];
    ++proof.events;
    proof.may_throw |=
        graph.ops[i].exceptional == ExceptionalCompletion::kMayThrow;
    const OpId id{static_cast<std::uint32_t>(i)};
    if(graph.ops[i].exceptional==ExceptionalCompletion::kMayThrow)proof.thrower=id;
    if (phases.HasCatchCopies(id) ||
        (graph.ops[i].exceptional == ExceptionalCompletion::kMayThrow &&
         !phases.SameCatch(id, proof.call)))
      invalid[allocation->value] = 1;
  }
  // Reprove ordinary operand trees BEFORE their membership can replace the
  // usual phase equations. Ownership flags alone cannot hide a reordered or
  // duplicated getter. LiteralPlan and selection certificates retain their
  // separate nested/protocol uses; implicit definition bases are not reads.
  std::vector<PhaseEffectInterval> intervals;
  std::vector<std::uint8_t> consumed;
  budget.Array(intervals, graph.ops.size());
  budget.Array(consumed, graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    const OpId id{static_cast<std::uint32_t>(i)};
    intervals[i] = phases.Interval(id);
    if (!owned[i]) continue;
    const auto allocation = *owned[i];
    const auto& op = graph.ops[i];
    const auto shape = DescribeEvaluation(op.operation);
    if (!shape || shape->Count() != op.use_count) {
      invalid[allocation.value] = 1;
      continue;
    }
    std::optional<PhaseEffectInterval> children;
    for (std::uint32_t j = 0; j < op.use_count; ++j) {
      budget.Step();
      const UseId use{op.first_use.value + j};
      const auto& occurrence = graph.uses[use.value];
      if (IsMemberReceiver(graph, plan, occurrence) ||
          (fields[i] && occurrence.role == OperandRole::kBase))
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
      if (interval.count > interval.end) invalid[allocation.value] = 1;
      if (std::holds_alternative<ValueBranch>(op.operation) && interval.count)
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
  for (std::size_t i = 0; i < owned.size(); ++i) {
    budget.Step();
    if (!owned[i] || i == owned[i]->value) continue;
    if (plan.operations[i].kind == EvaluationKind::kInline && !consumed[i] &&
        !CrossLogicalInline(graph, plan, {static_cast<std::uint32_t>(i)}))
      invalid[owned[i]->value] = 1;
  }
  budget.Admit(phase_budget.Cost().bytes, 1);
  budget.Step(phase_budget.Cost().work);
  for (std::size_t i = 0; i < result.size(); ++i) {
    budget.Step();
    if (invalid[i]) result[i].reset();
  }
  for (auto& member : owned) {
    budget.Step();
    if (member && !result[member->value]) member.reset();
  }
  return result;
}

std::vector<std::optional<LiteralReferenceView>> IndexLiteralReferences(
    const ValueProgram& graph, const EvaluationPlan& plan,
    TreeCheckBudget& budget, std::vector<std::optional<OpId>>* members) {
  std::vector<std::optional<OpId>> local;
  auto& owned=members?*members:local;
  auto result=IndexLegacyLiteralReferences(graph,plan,budget,&owned);
  if(plan.literal_fields.objects.empty())return result;
  if(result.empty())budget.Array(result,graph.ops.size());
  if(owned.empty())budget.Array(owned,graph.ops.size());
  for(const auto& object:plan.literal_fields.objects){
    budget.Step();
    result[object.allocation.value]=LiteralReferenceView{object.allocation,object.last,object.call,object.callee,object.argument};
  }
  for(std::size_t i=0;i<plan.literal_fields.members.size();++i){
    budget.Step();const auto owner=plan.literal_fields.members[i];if(!owner)continue;
    Require(!owned[i]||owned[i]==owner);owned[i]=owner;
    if(!HasOrderedEffect(graph.ops[i]))continue;
    auto& view=*result[owner->value];++view.events;
    if(graph.ops[i].exceptional==ExceptionalCompletion::kMayThrow){view.may_throw=true;view.thrower=OpId{static_cast<std::uint32_t>(i)};}
  }
  return result;
}

void BuildPhasedObjectOperands(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  auto selected = Objects(graph, plan, budget);
  bool any = false;
  for (const auto& object : selected) { budget.Step(); any |= object.has_value(); }
  if (!any) return;
  for (const auto& object : plan.literals->objects) {
    budget.Step(); if (!selected[object.allocation.value]) continue;
    for (std::uint32_t p = 0; p < object.count; ++p) {
      budget.Step(); const auto id = plan.literals->definitions[object.first + p];
      plan.operations[id.value] = {EvaluationKind::kInitializer, {}, {}};
    }
  }
  plan.logical_objects = std::move(selected);
}
void VerifyPhasedObjectOperands(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  // Validate the new certificate before any of its IDs can index the object
  // projection. This is also the early common-evaluation verification path.
  VerifyLiteralFieldForests(graph,plan,budget);
  const auto expected = Objects(graph, plan, budget);
  Require(!expected.empty() || plan.logical_objects.empty());
  Require(plan.logical_objects.empty() || plan.logical_objects.size() == graph.ops.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    budget.Step(); const auto actual = plan.logical_objects.empty() ? std::optional<OpId>{} : plan.logical_objects[i];
    Require(actual == expected[i]);
  }
  if (plan.literals) for (const auto& object : plan.literals->objects) {
    budget.Step(); if (expected.empty() || !expected[object.allocation.value]) continue;
    for (std::uint32_t p = 0; p < object.count; ++p) {
      budget.Step(); const auto& placement = plan.operations[plan.literals->definitions[object.first + p].value];
      Require(placement.kind == EvaluationKind::kInitializer && !placement.consumer && !placement.temporary);
    }
  }
}
}  // namespace abcd::source
