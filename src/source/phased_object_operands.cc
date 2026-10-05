#include "logical_evaluation.h"
#include "effect_order.h"
#include "phase_corridor.h"

namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool value) { if (!value) throw EvaluationError::kOwnership; }

// Source ownership is a functional forest, including selected phi arms and
// native nested initializers. Resolve each parent once; storage/PC order is not
// an ownership proof, and a cycle cannot become an executable source subtree.
std::vector<OpId> Owners(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
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
    if ((placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kNestedLiteral ||
         placement.kind == EvaluationKind::kPhiInput || placement.kind == EvaluationKind::kLogicalValue) &&
        placement.consumer) parents[i] = consumer(*placement.consumer);
    if (i < plan.phase_prefixes.size() && plan.phase_prefixes[i]) parents[i] = plan.phase_prefixes[i];
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
  return selected;
}
}  // namespace

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
