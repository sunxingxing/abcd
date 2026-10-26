#include "exception_stage_recovery.h"

#include <algorithm>

#include "flow_dominance.h"
#include "flow_motion.h"
#include "exception_copy_emission.h"

namespace abcd::source {
void VerifyExceptionStageRecovery(const RecoveryFlow& flow, TreeCheckBudget& budget) {
  using Error = TreeStructureError;
  const auto need = [](bool ok) { RequireTree(ok, Error::kOwnership); };
  if (!flow.original || flow.original->exception_stages.empty()) return;
  const auto built = AnalyzeFlowDominance(flow, budget.Remaining());
  if (const auto* error = std::get_if<Error>(&built)) throw *error;
  const auto& order = std::get<FlowDominance>(built);
  budget.Admit(order.allocated_bytes, 1); budget.Step(order.work);
  std::vector<std::optional<FlowDefinitionId>> definitions;
  budget.Array(definitions, flow.expressions.size());
  struct Saved { ExprId input; SymbolId binding; FlowDefinitionId definition; };
  std::vector<Saved> saved;
  budget.Admit(flow.definitions.size(), sizeof(Saved)); saved.reserve(flow.definitions.size());
  budget.Admit(saved.capacity() - flow.definitions.size(), sizeof(Saved));
  struct Write { StmtId owner; SymbolId binding; FlowDefinitionId definition; };
  std::vector<Write> writes;
  budget.Admit(flow.definitions.size(), sizeof(Write)); writes.reserve(flow.definitions.size());
  budget.Admit(writes.capacity() - flow.definitions.size(), sizeof(Write));
  for (std::size_t d = 0; d < flow.definitions.size(); ++d) {
    budget.Step(); const auto& definition = flow.definitions[d];
    if (definition.original && std::holds_alternative<OriginalSavedDefinition>(definition.original->origin))
      saved.push_back({definition.original->input, definition.original->binding, FlowDefinitionId{static_cast<std::uint32_t>(d)}});
    if (definition.write) {
      need(!definitions[definition.write->value]);
      definitions[definition.write->value] = FlowDefinitionId{static_cast<std::uint32_t>(d)};
    }
    if (const auto owner = flow.events[definition.point.value].owner)
      writes.push_back({*owner, definition.binding, FlowDefinitionId{static_cast<std::uint32_t>(d)}});
  }
  const auto saved_key = [](const Saved& row) { return std::pair{row.input, row.binding}; };
  std::sort(saved.begin(), saved.end(), [&](const auto& a, const auto& b) { budget.Step(); return saved_key(a) < saved_key(b); });
  const auto key = [](const Write& write) { return std::pair{write.owner, write.binding}; };
  std::sort(writes.begin(), writes.end(), [&](const auto& a, const auto& b) {
    budget.Step(); return key(a) < key(b);
  });
  enum class BoundaryKind : std::uint8_t { kStage, kExpression };
  struct Boundary { ExprId root; BoundaryKind kind; };
  std::vector<std::optional<Boundary>> boundaries;
  std::vector<FlowPoint> queries;
  budget.Array(boundaries, flow.events.size()); budget.Array(queries, flow.events.size());
  std::size_t query_count = 0;
  const auto boundary = [&](FlowPoint point, ExprId root, BoundaryKind kind) {
    budget.Step(); auto& previous = boundaries[point.value];
    if (previous) need(previous->root == root && previous->kind == kind);
    else { previous = Boundary{root, kind}; queries[query_count++] = point; }
  };
  // Current source expression evaluation is indivisible by statements. With a
  // private slot, its dominating prefix write and absence of competing writes
  // in that evaluation prove the exact reaching snapshot at every throw. The
  // owner comes from current flow, including trial projections, not raw PCs.
  // One shared write index avoids per-copy/per-variable graph traversals.
  for (const auto& stage : flow.original->exception_stages) {
    budget.Step(); need(stage.assignment.value < definitions.size());
    need(stage.edge.value < flow.original->edges.size());
    const auto& edge = flow.original->edges[stage.edge.value];
    need(edge.kind == analysis::FlowEdgeKind::kException && edge.target == stage.handler &&
        edge.source.value < flow.original->phases.size() &&
        flow.original->phases[edge.source.value].operation == stage.operation);
    auto id = definitions[stage.assignment.value];
    const auto& expected = flow.original->definitions[stage.assignment.value]; need(expected.has_value());
    if (!id) {
      const auto wanted = std::pair{expected->input, expected->binding};
      const auto begin = std::lower_bound(saved.begin(), saved.end(), wanted,
          [&](const auto& a, auto b) { budget.Step(); return saved_key(a) < b; });
      const auto end = std::upper_bound(begin, saved.end(), wanted,
          [&](auto a, const auto& b) { budget.Step(); return a < saved_key(b); });
      need(end - begin == 1); id = begin->definition;
    }
    const auto& definition = flow.definitions[id->value];
    need(definition.original && std::holds_alternative<OriginalSavedDefinition>(definition.original->origin));
    const auto& physical = std::get<OriginalSavedDefinition>(definition.original->origin);
    const auto& original = std::get<OriginalSavedDefinition>(expected->origin);
    const auto* physical_entry = std::get_if<analysis::OpId>(&physical.placement);
    const auto* original_entry = std::get_if<analysis::OpId>(&original.placement);
    need(physical_entry && original_entry && *original_entry == stage.entry &&
        *physical_entry == *original_entry &&
        SameStageOperand(physical.operand, original.operand));
    const auto& binding = flow.bindings[definition.binding.value];
    need(binding.exposure == FlowBindingExposure::kPrivate && !binding.MayBeChangedByUserCode());
    need(stage.operation.value < flow.original_actions.size());
    auto action = flow.original_actions[stage.operation.value];
    if (FindOriginalActionRegion(*flow.original, stage.operation)) {
      const auto id = flow.original->action_region_members[stage.operation.value];
      need(id && id->value < flow.original_regions.size());
      if (const auto native = flow.original_regions[id->value])
        action = FlowOriginalAction{native->before, native->normal, native->exceptions};
    } else if (const auto& guard = flow.original->operations[stage.operation.value].initialized_guard) {
      // The existing guard correspondence locates its checked native GetValue;
      // neither a lexical anchor nor an initialized spelling supplies a port.
      need(FindFlowOriginalGuard(flow, stage.operation).has_value());
      action = flow.original_actions[guard->load.value];
    }
    need(action.has_value());
    need(order.Dominates(definition.point, action->before));
    need(stage.root.value < flow.expression_owners.size());
    const auto owner = flow.expression_owners[stage.root.value]; need(owner.has_value());
    need(flow.events[action->before.value].owner == owner);
    need(stage.root.value < flow.expressions.size());
    const auto entry = flow.expressions[stage.root.value];
    need(order.Dominates(definition.point, entry));
    boundary(definition.point, stage.root, BoundaryKind::kStage);
    boundary(entry, stage.root, BoundaryKind::kExpression);
    const auto wanted = std::pair{*owner, definition.binding};
    const auto begin = std::lower_bound(writes.begin(), writes.end(), wanted,
        [&](const auto& a, auto b) { budget.Step(); return key(a) < b; });
    const auto end = std::upper_bound(begin, writes.end(), wanted,
        [&](auto a, const auto& b) { budget.Step(); return a < key(b); });
    // At most the same prefix definition may now be inside the expression.
    need(end - begin <= 1 && (begin == end || begin->definition == *id));
    const auto handler = FindOriginalPhase(flow, stage.handler, analysis::ExceptionPhase::kCatch);
    need(handler && action->exceptions.thrown == *handler && !action->exceptions.finalizer);
  }
  // Static dominance alone would permit one stage outside a repeated loop.
  // Pair universal boundary histories in BOTH directions, through every normal
  // and exceptional completion, so each stage executes once per expression.
  const auto counted = AnalyzeFlowBoundaryMotion(flow,
      std::span<const FlowPoint>(queries).first(query_count), budget.Remaining());
  if (const auto* error = std::get_if<Error>(&counted)) throw *error;
  const auto& motion = std::get<FlowMotion>(counted);
  budget.Admit(motion.allocated_bytes, 1); budget.Step(motion.work);
  for (std::size_t i = 0; i < query_count; ++i) {
    budget.Step(); const auto point = queries[i]; const auto here = *boundaries[point.value];
    if (here.kind == BoundaryKind::kStage) {
      const auto next = motion.next[point.value]; need(next.has_value());
      need(boundaries[next->value] && boundaries[next->value]->root == here.root &&
          motion.previous[next->value] == point);
    } else {
      const auto previous = motion.previous[point.value]; need(previous.has_value());
      need(boundaries[previous->value] && boundaries[previous->value]->root == here.root &&
          boundaries[previous->value]->kind == BoundaryKind::kStage);
    }
  }
}
}  // namespace abcd::source
