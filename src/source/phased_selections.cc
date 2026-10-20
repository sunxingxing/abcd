#include <algorithm>

#include "evaluation_liveness.h"
#include "evaluation_plan.h"
#include "exception_control.h"
#include "literal_event_view.h"
#include "logical_evaluation.h"
#include "logical_regions.h"
#include "phase_predicate_prefix.h"
#include "phased_selection_paths.h"
#include "tree_checks.h"

namespace abcd::source {
std::vector<SelectionRegion> FindPhasedPhiSelections(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, TreeCheckBudget& budget) {
  using namespace analysis;
  std::vector<SelectionRegion> result;
  if (!evaluation.phis || evaluation.phis->flow != PhiFlow::kPhased) return result;
  std::vector<std::optional<UseId>> inputs;
  std::vector<std::uint32_t> copies, uses, incoming;
  budget.Array(inputs, graph.edges.size()); budget.Array(copies, graph.edges.size());
  budget.Array(uses, graph.values.size()); budget.Array(incoming, graph.blocks.size());
  for (const auto& edge : graph.edges) {
    budget.Step(); if (edge.kind != FlowEdgeKind::kException) ++incoming[edge.target.value];
  }
  for (const auto& bundle : evaluation.phis->bundles) {
    budget.Step(); const auto* edge = std::get_if<EdgeId>(&bundle.incoming);
    if (!edge) continue;
    copies[edge->value] = static_cast<std::uint32_t>(bundle.schedule.steps.size());
    if (bundle.expression_input && bundle.schedule.steps.size() == 1 && !bundle.schedule.temporaries)
      inputs[edge->value] = bundle.expression_input;
  }
  for (const auto& use : graph.uses) {
    budget.Step();
    if (use.life == ValueLife::kLive)
      if (const auto* value = std::get_if<ValueId>(&use.owner)) ++uses[value->value];
  }
  std::vector<std::uint8_t> staged;
  budget.Array(staged, graph.ops.size());
  const auto roots = IndexPhaseInlineRoots(graph, evaluation, budget);
  if (evaluation.exceptions) for (std::size_t i = 0; i < evaluation.exceptions->placements.size(); ++i) {
    budget.Step();
    const auto* stage = std::get_if<ThrowCopies>(&evaluation.exceptions->placements[i]);
    if (stage && evaluation.phis->bundles[i].count) staged[stage->operation.value] = 1;
  }
  std::optional<LiteralEventView> literal_events;
  if (!evaluation.literal_arm_inputs.empty())
    literal_events = BuildLiteralEventView(graph, *evaluation.literals, budget);
  struct Arm { WorkBlockId tail; EdgeId edge; SelectionOperand value; ValueId merged; };
  const auto arm = [&](EdgeId entry, std::size_t first_incoming = 1) -> std::optional<Arm> {
    auto at = graph.edges[entry.value].target;
    std::optional<OpId> producer;
    if (copies[entry.value]) return {};
    // Follow original normal edges only for candidate discovery, while
    // retaining every exception edge in the authoritative graph. Single-entry
    // tails stop at the first phi edge, merge, nested branch or catch entry;
    // different candidate arms cannot rescan a shared suffix.
    for (std::size_t visited = 0; visited < graph.blocks.size(); ++visited) {
      budget.Step(); const auto& block = graph.blocks[at.value];
      if (incoming[at.value] != (visited == 0 ? first_incoming : 1) || graph.phases[at.value].phase == ExceptionPhase::kCatch) return {};
      std::optional<EdgeId> normal;
      for (std::uint32_t e = 0; e < block.edge_count; ++e) {
        budget.Step(); const EdgeId edge{block.first_edge.value + e};
        if (graph.edges[edge.value].kind == FlowEdgeKind::kException) continue;
        if (normal) return {};
        normal = edge;
      }
      if (!normal) return {};
      for (std::uint32_t i = 0; i < block.op_count; ++i) {
        budget.Step(); const OpId id{block.first_op.value + i}; const auto& op = graph.ops[id.value];
        if (op.life != ValueLife::kLive) continue;
        if (staged[id.value]) return {};
        const auto kind = evaluation.operations[id.value].kind;
        if (kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure ||
            std::holds_alternative<ValueJump>(op.operation) || std::holds_alternative<ValueNop>(op.operation)) {
          if (op.normal != LanguageEffect::kNone || op.before_throw != LanguageEffect::kNone ||
              op.exceptional != ExceptionalCompletion::kImpossible) return {};
          continue;
        }
        if (literal_events && literal_events->events[id.value]) {
          const auto root = literal_events->events[id.value]->allocation;
          if (evaluation.literal_arm_inputs[root.value]) {
            if (producer && *producer != root) return {};
            producer = root;
            continue;
          }
        }
        const bool guarded = kind == EvaluationKind::kGuardedCellRead &&
            !GuardedInput(evaluation,id) && GuardedPeer(evaluation,id) &&
            evaluation.operations[GuardedPeer(evaluation,id)->value].kind == EvaluationKind::kInline;
        if (kind != EvaluationKind::kPhiInput && kind != EvaluationKind::kInline &&
            kind != EvaluationKind::kModuleGuard && !guarded) return {};
        const auto root = roots[id.value];
        if (evaluation.operations[root.value].kind != EvaluationKind::kPhiInput ||
            !graph.ops[root.value].result || (producer && *producer != root)) return {};
        producer = root;
      }
      const auto edge = *normal;
      if (graph.edges[edge.value].kind == FlowEdgeKind::kException) return {};
      if (copies[edge.value]) {
        if (!inputs[edge.value]) return {};
        const auto input = *inputs[edge.value]; const auto& use = graph.uses[input.value];
        const auto* value = std::get_if<ValueId>(&use.operand);
        const auto* merged = std::get_if<ValueId>(&use.owner);
        if (!value || !merged) return {};
        if (producer) {
          const bool literal =
              !evaluation.literal_arm_inputs.empty() &&
              evaluation.literal_arm_inputs[producer->value] == input;
          const auto actual =
              literal ? ResolveEvaluationOperand(evaluation, use.operand)
                      : use.operand;
          const auto* final = std::get_if<ValueId>(&actual);
          if (!final || graph.ops[producer->value].result != *final ||
              evaluation.operations[producer->value].consumer != input)
            return {};
          return Arm{at, edge, *final, *merged};
        }
        // An inert arm forwards an original available binding. Resolve verified
        // copy aliases once; preserve the first phi/catch materialization and
        // all authoritative exception edges instead of replaying its producer.
        const auto resolved = ResolveEvaluationOperand(evaluation, use.operand);
        const auto* retained = std::get_if<ValueId>(&resolved);
        if (!retained || !HasRetainedEvaluationValue(graph, evaluation, *retained)) return {};
        return Arm{at, edge, SelectionSnapshot{*retained}, *merged};
      }
      at = graph.edges[edge.value].target;
    }
    return {};
  };
  struct ConditionLink {
    WorkBlockId next;
    bool shared_on_taken = false, next_shared_on_taken = false;
    std::vector<WorkBlockId> path;
  };
  std::vector<std::optional<ConditionLink>> links;
  std::vector<std::uint8_t> parent;
  std::vector<WorkBlockId> scratch;
  budget.Array(links, graph.blocks.size()); budget.Array(parent, graph.blocks.size());
  budget.Array(scratch, graph.blocks.size());
  const auto next_condition = [&](EdgeId entry) -> std::optional<ConditionLink> {
    if (copies[entry.value]) return {};
    auto at = graph.edges[entry.value].target; std::size_t count = 0;
    for (; count < graph.blocks.size();) {
      budget.Step();
      if (incoming[at.value] != 1 || graph.phases[at.value].phase == ExceptionPhase::kCatch) return {};
      scratch[count++] = at;
      const auto& block = graph.blocks[at.value]; const auto& phase = graph.phases[at.value];
      if (phase.phase == ExceptionPhase::kCommit && phase.operation && block.edge_count == 2 &&
          std::holds_alternative<ValueBranch>(graph.ops[phase.operation->value].operation)) {
        ConditionLink link{at}; budget.Array(link.path, count);
        std::copy_n(scratch.begin(), count, link.path.begin()); return link;
      }
      for (std::uint32_t i = 0; i < block.op_count; ++i) {
        budget.Step(); const OpId id{block.first_op.value + i}; const auto& op = graph.ops[id.value];
        if (op.life != ValueLife::kLive) continue;
        const auto kind = evaluation.operations[id.value].kind;
        if (staged[id.value] || op.normal != LanguageEffect::kNone || op.before_throw != LanguageEffect::kNone ||
            op.exceptional != ExceptionalCompletion::kImpossible) return {};
        if (kind != EvaluationKind::kInline && kind != EvaluationKind::kAlias && kind != EvaluationKind::kElidedPure &&
            !std::holds_alternative<ValueBranch>(op.operation) && !std::holds_alternative<ValueJump>(op.operation) &&
            !std::holds_alternative<ValueNop>(op.operation)) return {};
      }
      if (block.edge_count != 1 || copies[block.first_edge.value] ||
          graph.edges[block.first_edge.value].kind == FlowEdgeKind::kException) return {};
      at = graph.edges[block.first_edge.value].target;
    }
    return {};
  };
  // Index adjacent scalar tests once. Mark chain children so a long shared-arm
  // decision is visited only from its maximal root, never once per suffix.
  for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
    budget.Step(); const auto& phase = graph.phases[b]; const auto& block = graph.blocks[b];
    if (phase.phase != ExceptionPhase::kCommit || !phase.operation || block.edge_count != 2 ||
        !std::holds_alternative<ValueBranch>(graph.ops[phase.operation->value].operation)) continue;
    for (std::uint32_t side = 0; side != 2; ++side) {
      const auto shared = graph.edges[block.first_edge.value + side].target;
      if (incoming[shared.value] < 2) continue;
      auto link = next_condition(EdgeId{block.first_edge.value + (1 - side)});
      if (!link) continue;
      const auto& next = graph.blocks[link->next.value];
      const bool taken = graph.edges[next.first_edge.value].target == shared;
      const bool other = graph.edges[next.first_edge.value + 1].target == shared;
      if (taken == other || parent[link->next.value]) continue;
      link->shared_on_taken = side == 0; link->next_shared_on_taken = taken;
      parent[link->next.value] = 1; links[b] = std::move(link); break;
    }
  }
  budget.Admit(graph.blocks.size(), sizeof(SelectionRegion)); result.reserve(graph.blocks.size());
  budget.Admit(result.capacity() - graph.blocks.size(), sizeof(SelectionRegion));
  for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
    budget.Step(); const WorkBlockId header_id{static_cast<std::uint32_t>(b)};
    const auto& phase = graph.phases[b];
    if (parent[b] || phase.phase != ExceptionPhase::kCommit || !phase.operation) continue;
    const auto branch = *phase.operation; const auto& op = graph.ops[branch.value];
    if (!std::holds_alternative<ValueBranch>(op.operation) || op.use_count != 1) continue;
    const auto* condition = std::get_if<ValueId>(&graph.uses[op.first_use.value].operand);
    const auto* predicate = condition ? std::get_if<OpResult>(&graph.values[condition->value].origin) : nullptr;
    if (!predicate) continue;
    const auto& header = graph.blocks[b];
    if (header.edge_count != 2) continue;
    std::vector<SelectionCondition> conditions;
    std::vector<WorkBlockId> phases;
    std::size_t condition_count = 1, phase_count = 0;
    auto last = header_id;
    for (std::size_t visited = 0; links[last.value] && visited < graph.blocks.size(); ++visited) {
      budget.Step(); ++condition_count; phase_count += links[last.value]->path.size(); last = links[last.value]->next;
    }
    if (condition_count > graph.blocks.size()) continue;
    budget.Array(conditions, condition_count); budget.Array(phases, phase_count);
    conditions[0] = {branch, links[b] ? links[b]->shared_on_taken : true};
    std::size_t condition_at = 1, phase_at = 0; auto cursor = header_id;
    while (links[cursor.value]) {
      budget.Step(); const auto& link = *links[cursor.value];
      conditions[condition_at++] = {*graph.phases[link.next.value].operation, link.next_shared_on_taken};
      for (const auto block : link.path) { budget.Step(); phases[phase_at++] = block; }
      cursor = link.next;
    }
    const EdgeId shared{header.first_edge.value + (conditions.front().shared_on_taken ? 0U : 1U)};
    const EdgeId fallback{graph.blocks[last.value].first_edge.value + (conditions.back().shared_on_taken ? 1U : 0U)};
    const auto yes = arm(shared, condition_count);
    const auto no = arm(fallback);
    if (!yes || !no || yes->edge == no->edge || yes->merged != no->merged || uses[yes->merged.value] != 2) continue;
    const auto join = graph.edges[yes->edge.value].target;
    const auto* merged = std::get_if<PhiDefinition>(&graph.values[yes->merged.value].origin);
    if (!merged || merged->block != join || graph.edges[no->edge.value].target != join) continue;
    SelectionRegion region{TreeLogical::kOr, predicate->op, branch, yes->merged, *condition,
        no->value, header_id, yes->tail, no->tail, join, yes->edge, no->edge};
    region.consequent = yes->value;
    // Retain the original branch predicate (including VM boolean-vs-zero
    // semantics), rather than assuming JavaScript truthiness from source style.
    region.conditions = std::move(conditions); region.condition_phases = std::move(phases);
    result.push_back(std::move(region));
  }
  auto nested = NestedPhaseSelections(graph, evaluation, budget);
  if (!nested.empty()) {
    std::vector<std::uint8_t> claimed; budget.Array(claimed, graph.values.size());
    for (const auto& region : nested) { budget.Step(); claimed[region.merged.value] = 1; }
    std::size_t kept = 0;
    for (std::size_t i = 0; i < result.size(); ++i) {
      budget.Step(); if (claimed[result[i].merged.value]) continue;
      if (kept != i) result[kept] = std::move(result[i]);
      ++kept;
    }
    result.resize(kept);
    for (auto& region : nested) { budget.Step(); result.push_back(std::move(region)); }
  }
  return result;
}
// Bind graph candidates to the actual exception-control owner before emission.
// Control IDs never substitute for phase-block IDs in the discovery algorithm.
std::vector<SelectionRegion> FindPhasedSelections(const analysis::ValueProgram& graph,
    const ControlPlan& control, const EvaluationPlan& evaluation, TreeCheckBudget& budget,
    const ExceptionControl* projection) {
  // Control nodes address projection units, not original phase blocks. Close
  // and forwarded-handler removal can change every later index. Bind only
  // PhaseControlUnit identities; wrappers/close ports cannot own a selection.
  // Both emission and TreeFlow rebuild this binding after control verification.
  const auto source_block = [&](const ControlNode& node) -> std::optional<analysis::WorkBlockId> {
    if (!std::holds_alternative<std::monostate>(node.synthetic)) return {};
    if (!projection) return node.block;
    const auto index = static_cast<std::size_t>(&node - control.nodes.data());
    if (index >= projection->units.size()) return {};
    const auto* phase = std::get_if<PhaseControlUnit>(&projection->units[index]);
    return phase ? std::optional{phase->block} : std::nullopt;
  };
  using namespace analysis;
  auto regions = FindPhasedPhiSelections(graph, evaluation, budget);
  auto bindings = FindPhaseSelectionBindings(graph, evaluation, regions, budget);
  std::vector<std::optional<std::size_t>> binding_indices;
  budget.Array(binding_indices, graph.ops.size());
  for (std::size_t i = 0; i < bindings.size(); ++i) { budget.Step(); binding_indices[bindings[i].branch.value] = i; }
  std::vector<std::optional<ControlId>> headers;
  std::vector<std::uint8_t> counts;
  budget.Array(headers, graph.blocks.size()); budget.Array(counts, graph.blocks.size());
  for (std::size_t i = 0; i < control.nodes.size(); ++i) {
    budget.Step(); const auto& node = control.nodes[i];
    const auto block = source_block(node);
    if (!node.arms || !block || block->value >= headers.size()) continue;
    if (counts[block->value]) { headers[block->value].reset(); continue; }
    counts[block->value] = 1; headers[block->value] = ControlId{static_cast<std::uint32_t>(i)};
  }
  const auto path = [&](std::optional<ControlId> first, EdgeId edge, WorkBlockId terminal, bool child) {
    for (std::size_t visited = 0; first && visited < control.nodes.size(); ++visited) {
      budget.Step(); const auto& node = control.nodes[first->value];
      const auto original = source_block(node);
      if (!original || original != graph.edges[edge.value].target) return false;
      if (original == terminal) return child ? node.arms.has_value() : !node.arms && !node.next;
      if (node.arms || node.decision) return false;
      const auto& block = graph.blocks[original->value];
      std::optional<EdgeId> normal;
      for (std::uint32_t e = 0; e < block.edge_count; ++e) {
        budget.Step(); const EdgeId candidate{block.first_edge.value + e};
        if (graph.edges[candidate.value].kind == FlowEdgeKind::kException) continue;
        if (normal) return false;
        normal = candidate;
      }
      if (!normal) return false;
      edge = *normal; first = node.next;
    }
    return false;
  };
  std::size_t kept = 0;
  for (auto& region : regions) {
    budget.Step(); if (!headers[region.header.value]) continue;
    // A fused family needs its checked execution owner; a retained binding
    // needs original control ownership. Discovery grants neither placement.
    if (region.parent) {
      if (!binding_indices[region.branch.value] && (region.branch.value >= evaluation.phase_children.size() ||
          evaluation.phase_children[region.branch.value] != region.parent)) continue;
    } else if (std::holds_alternative<SelectionChild>(region.right) ||
               std::holds_alternative<SelectionChild>(*region.consequent)) {
      const auto* owner = FindPhaseSelectionOperand(graph, evaluation, region.branch);
      if (!owner && !binding_indices[region.branch.value]) continue;
    }
    const auto& node = control.nodes[headers[region.header.value]->value];
    if (region.parent) {
      if (node.next && source_block(control.nodes[node.next->value]) != region.join) continue;
    } else if (!node.next || source_block(control.nodes[node.next->value]) != region.join) continue;
    const auto* last = &node; std::size_t phase = 0; bool bound = true;
    budget.Array(region.condition_blocks, region.condition_phases.size());
    for (std::size_t i = 1; i < region.conditions.size() && bound; ++i) {
      budget.Step();
      if (!last->decision || last->decision->evaluate_on_taken == region.conditions[i - 1].shared_on_taken ||
          last->decision->shared_on_taken != region.conditions[i].shared_on_taken) { bound = false; break; }
      auto at = last->decision->evaluate; const auto terminal = last->decision->predicate.value_or(at);
      for (std::size_t visited = 0; visited < control.nodes.size(); ++visited) {
        budget.Step(); const auto& part = control.nodes[at.value];
        if (phase >= region.condition_phases.size() || source_block(part) != region.condition_phases[phase] ||
            !std::holds_alternative<std::monostate>(part.synthetic)) { bound = false; break; }
        region.condition_blocks[phase++] = at;
        if (at == terminal) { last = &part; break; }
        if (part.arms || part.decision || !part.next) { bound = false; break; }
        at = *part.next;
      }
      if (!bound || graph.phases[source_block(*last)->value].operation != region.conditions[i].branch) bound = false;
    }
    if (!bound || phase != region.condition_phases.size() || last->decision) continue;
    const bool first_taken = region.conditions.front().shared_on_taken;
    const bool last_taken = region.conditions.back().shared_on_taken;
    const auto first = graph.blocks[region.header.value].first_edge;
    const auto end = graph.blocks[source_block(*last)->value].first_edge;
    const auto shared = first_taken ? node.arms->taken : node.arms->fallthrough;
    const auto fallback = last_taken ? last->arms->fallthrough : last->arms->taken;
    if (!path(shared, EdgeId{first.value + (first_taken ? 0U : 1U)}, *region.keep,
            std::holds_alternative<SelectionChild>(*region.consequent)) ||
        !path(fallback, EdgeId{end.value + (last_taken ? 1U : 0U)}, region.evaluate,
            std::holds_alternative<SelectionChild>(region.right))) continue;
    region.keep_path = shared; region.evaluate_path = fallback;
    if (const auto index = binding_indices[region.branch.value])
      region.original_prefix = std::move(bindings[*index].prefix);
    if (&regions[kept] != &region) regions[kept] = std::move(region);
    ++kept;
  }
  regions.resize(kept);
  // No prefix is consumed before all control children have a bound owner.
  // This also preserves old statement output across a try-wrapper boundary.
  std::vector<std::optional<std::size_t>> indices; budget.Array(indices, graph.ops.size());
  std::vector<std::uint8_t> closed; budget.Array(closed, regions.size());
  for (std::size_t i = 0; i < regions.size(); ++i) { budget.Step(); indices[regions[i].branch.value] = i; }
  for (std::size_t i = 0; i < regions.size(); ++i) {
    budget.Step(); bool complete = true;
    for (const auto* arm : {&regions[i].right, &*regions[i].consequent}) {
      if (const auto* child = std::get_if<SelectionChild>(arm))
        complete &= indices[child->branch.value] && closed[*indices[child->branch.value]];
    }
    closed[i] = complete;
  }
  for (std::size_t i = regions.size(); i-- > 0;) {
    budget.Step(); if (const auto parent = regions[i].parent)
      closed[i] &= indices[parent->value] && closed[*indices[parent->value]];
  }
  kept = 0;
  for (std::size_t i = 0; i < regions.size(); ++i) {
    budget.Step(); if (!closed[i]) continue;
    if (kept != i) regions[kept] = std::move(regions[i]);
    ++kept;
  }
  regions.resize(kept); return regions;
}
}  // namespace abcd::source
