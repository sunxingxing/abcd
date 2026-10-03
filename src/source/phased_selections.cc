#include "logical_regions.h"
#include "evaluation_plan.h"
#include "tree_checks.h"

namespace abcd::source {
std::vector<SelectionRegion> FindPhasedSelections(const analysis::ValueProgram& graph,
    const ControlPlan& control, const EvaluationPlan& evaluation, TreeCheckBudget& budget) {
  using namespace analysis;
  std::vector<SelectionRegion> result;
  if (!evaluation.phis || evaluation.phis->flow != PhiFlow::kPhased) return result;
  std::vector<std::optional<UseId>> inputs;
  std::vector<std::uint32_t> copies, uses;
  budget.Array(inputs, graph.edges.size()); budget.Array(copies, graph.edges.size());
  budget.Array(uses, graph.values.size());
  for (const auto& bundle : evaluation.phis->bundles) {
    budget.Step(); const auto* edge = std::get_if<EdgeId>(&bundle.incoming);
    if (!edge) continue;
    copies[edge->value] = bundle.count;
    if (bundle.count == 1 && bundle.schedule.steps.size() == 1 && !bundle.schedule.temporaries)
      inputs[edge->value] = evaluation.phis->inputs[bundle.first];
  }
  for (const auto& use : graph.uses) {
    budget.Step();
    if (use.life == ValueLife::kLive)
      if (const auto* value = std::get_if<ValueId>(&use.owner)) ++uses[value->value];
  }
  std::vector<OpId> roots;
  std::vector<std::uint8_t> staged;
  budget.Array(roots, graph.ops.size()); budget.Array(staged, graph.ops.size());
  for (std::size_t i = graph.ops.size(); i-- > 0;) {
    budget.Step(); roots[i] = OpId{static_cast<std::uint32_t>(i)};
    const auto& placement = evaluation.operations[i];
    if (placement.kind != EvaluationKind::kInline || !placement.consumer) continue;
    const auto* owner = std::get_if<OpId>(&graph.uses[placement.consumer->value].owner);
    if (owner && owner->value > i) roots[i] = roots[owner->value];
  }
  for (const auto& module : evaluation.modules) {
    budget.Step(); if (module.pair.guard) roots[module.pair.guard->value] = roots[module.pair.read.value];
  }
  if (evaluation.exceptions) for (std::size_t i = 0; i < evaluation.exceptions->placements.size(); ++i) {
    budget.Step();
    const auto* stage = std::get_if<ThrowCopies>(&evaluation.exceptions->placements[i]);
    if (stage && evaluation.phis->bundles[i].count) staged[stage->operation.value] = 1;
  }
  struct Arm { WorkBlockId tail; EdgeId edge; ValueId value; ValueId merged; };
  const auto arm = [&](ControlId first, WorkBlockId join, EdgeId entry) -> std::optional<Arm> {
    auto at = first;
    std::optional<OpId> producer;
    if (copies[entry.value]) return {};
    // Each admitted arm is an existing single-entry control path. Nested
    // regions stop discovery; no exception edges or synthetic units are erased.
    for (std::size_t visited = 0; visited < control.nodes.size(); ++visited) {
      budget.Step(); const auto& node = control.nodes[at.value];
      if (node.arms || node.decision || !std::holds_alternative<std::monostate>(node.synthetic) ||
          node.block.value >= graph.blocks.size() || graph.edges[entry.value].target != node.block) return {};
      const auto& block = graph.blocks[node.block.value];
      if (graph.phases[node.block.value].phase == ExceptionPhase::kCatch) return {};
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
        if (kind != EvaluationKind::kPhiInput && kind != EvaluationKind::kInline && kind != EvaluationKind::kModuleGuard) return {};
        const auto root = roots[id.value];
        if (evaluation.operations[root.value].kind != EvaluationKind::kPhiInput ||
            !graph.ops[root.value].result || (producer && *producer != root)) return {};
        producer = root;
      }
      const auto edge = *normal;
      if (graph.edges[edge.value].kind == FlowEdgeKind::kException) return {};
      if (!node.next) {
        if (!producer || graph.edges[edge.value].target != join || !inputs[edge.value]) return {};
        const auto input = *inputs[edge.value]; const auto& use = graph.uses[input.value];
        const auto* value = std::get_if<ValueId>(&use.operand);
        const auto* merged = std::get_if<ValueId>(&use.owner);
        if (!value || !merged || graph.ops[producer->value].result != *value ||
            evaluation.operations[producer->value].consumer != input) return {};
        return Arm{node.block, edge, *value, *merged};
      }
      if (copies[edge.value]) return {};
      entry = edge; at = *node.next;
    }
    return {};
  };
  budget.Admit(control.nodes.size(), sizeof(SelectionRegion)); result.reserve(control.nodes.size());
  budget.Admit(result.capacity() - control.nodes.size(), sizeof(SelectionRegion));
  for (const auto& node : control.nodes) {
    budget.Step();
    if (!node.arms || !node.arms->taken || !node.arms->fallthrough || !node.next || node.decision ||
        !std::holds_alternative<std::monostate>(node.synthetic) || node.block.value >= graph.phases.size()) continue;
    const auto& phase = graph.phases[node.block.value];
    if (phase.phase != ExceptionPhase::kCommit || !phase.operation) continue;
    const auto branch = *phase.operation; const auto& op = graph.ops[branch.value];
    if (!std::holds_alternative<ValueBranch>(op.operation) || op.use_count != 1) continue;
    const auto* condition = std::get_if<ValueId>(&graph.uses[op.first_use.value].operand);
    const auto* predicate = condition ? std::get_if<OpResult>(&graph.values[condition->value].origin) : nullptr;
    if (!predicate) continue;
    const auto join = control.nodes[node.next->value].block;
    const auto& header = graph.blocks[node.block.value];
    if (header.edge_count != 2) continue;
    const auto yes = arm(*node.arms->taken, join, header.first_edge);
    const auto no = arm(*node.arms->fallthrough, join, EdgeId{header.first_edge.value + 1});
    if (!yes || !no || yes->edge == no->edge || yes->merged != no->merged || uses[yes->merged.value] != 2) continue;
    const auto* merged = std::get_if<PhiDefinition>(&graph.values[yes->merged.value].origin);
    if (!merged || merged->block != join) continue;
    SelectionRegion region{TreeLogical::kOr, predicate->op, branch, yes->merged, *condition,
        no->value, node.block, yes->tail, no->tail, join, yes->edge, no->edge};
    region.consequent = yes->value; region.keep_path = node.arms->taken; region.evaluate_path = node.arms->fallthrough;
    // Retain the original branch predicate (including VM boolean-vs-zero
    // semantics), rather than assuming JavaScript truthiness from source style.
    budget.Array(region.conditions, 1); region.conditions[0] = {branch, true};
    result.push_back(std::move(region));
  }
  return result;
}
}  // namespace abcd::source
