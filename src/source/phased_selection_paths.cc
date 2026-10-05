#include "phased_selection_paths.h"
#include "evaluation_plan.h"
#include "evaluation_liveness.h"
#include "logical_evaluation.h"
#include "phase_predicate_prefix.h"
#include "tree_checks.h"
namespace abcd::source {
using namespace analysis;
namespace {
bool InertSelectionOperation(const ValueOp& op) {
  return op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
      op.exceptional == ExceptionalCompletion::kImpossible;
}
// Closed paths are indexed once from successors to predecessors. A child is a
// SelectionRegion over original phi inputs, never a fabricated intermediate phi.
// Cycles, competing source observations and non-expression effects stay opaque;
// execution/catch admission remains with the common operand analysis.
}  // namespace
std::vector<SelectionRegion> NestedPhaseSelections(const ValueProgram& graph,
    const EvaluationPlan& plan, TreeCheckBudget& budget) {
  struct Path {
    ValueId value;
    WorkBlockId tail, join;
    std::optional<EdgeId> incoming;
    SelectionOperand operand;
    std::size_t leaves = 1;
    std::optional<std::size_t> child;
  };
  struct Visit { WorkBlockId block; std::uint32_t edge = 0; };
  enum class State : std::uint8_t { kUnseen, kActive, kDone };
  std::vector<std::optional<Path>> paths;
  std::vector<std::optional<UseId>> inputs;
  std::vector<std::optional<OpId>> leaf_parents;
  std::vector<std::size_t> leaves;
  std::vector<State> color;
  std::vector<Visit> stack;
  std::vector<WorkBlockId> order;
  std::vector<std::optional<OpId>> prefix_owner;
  budget.Array(paths, graph.blocks.size()); budget.Array(color, graph.blocks.size());
  budget.Array(stack, graph.blocks.size()); budget.Array(order, graph.blocks.size());
  budget.Array(inputs, graph.edges.size()); budget.Array(leaves, graph.values.size());
  budget.Array(leaf_parents, graph.edges.size());
  budget.Array(prefix_owner, graph.ops.size());
  const auto roots = IndexPhaseInlineRoots(graph, plan, budget);
  const auto prefixes = IndexPhasePredicatePrefixes(graph, plan, roots, budget);
  for (std::size_t b = 0; b < prefixes.ranges.size(); ++b) {
    budget.Step(); const auto range = prefixes.ranges[b];
    for (std::size_t i = 0; i < range.count; ++i) {
      budget.Step(); prefix_owner[prefixes.operations[range.first + i].value] = OpId{static_cast<std::uint32_t>(b)};
    }
  }
  for (const auto& use : graph.uses) {
    budget.Step(); if (use.life != ValueLife::kLive) continue;
    if (const auto* value = std::get_if<ValueId>(&use.owner)) ++leaves[value->value];
  }
  for (const auto& bundle : plan.phis->bundles) {
    budget.Step(); const auto* edge = std::get_if<EdgeId>(&bundle.incoming);
    if (edge && bundle.expression_input && bundle.schedule.steps.size() == 1 && !bundle.schedule.temporaries)
      inputs[edge->value] = bundle.expression_input;
  }
  std::size_t ordered = 0;
  for (std::size_t start = 0; start < graph.blocks.size(); ++start) {
    budget.Step(); if (color[start] != State::kUnseen) continue;
    std::size_t depth = 1; stack[0] = {WorkBlockId{static_cast<std::uint32_t>(start)}}; color[start] = State::kActive;
    while (depth) {
      budget.Step(); auto& visit = stack[depth - 1]; const auto& block = graph.blocks[visit.block.value];
      if (visit.edge == block.edge_count) { color[visit.block.value] = State::kDone; order[ordered++] = visit.block; --depth; continue; }
      const auto& edge = graph.edges[block.first_edge.value + visit.edge++];
      if (edge.kind == FlowEdgeKind::kException || color[edge.target.value] != State::kUnseen) continue;
      color[edge.target.value] = State::kActive; stack[depth++] = {edge.target};
    }
  }
  std::vector<SelectionRegion> regions;
  budget.Admit(graph.blocks.size(), sizeof(SelectionRegion)); regions.reserve(graph.blocks.size());
  budget.Admit(regions.capacity() - graph.blocks.size(), sizeof(SelectionRegion));
  const auto accepts = [&](WorkBlockId block, const Path& path) {
    const auto& range = graph.blocks[block.value];
    const auto child = path.child ? std::optional{regions[*path.child].branch} : std::nullopt;
    const auto* value = std::get_if<ValueId>(&path.operand);
    const auto* result = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
    for (std::uint32_t i = 0; i < range.op_count; ++i) {
      budget.Step(); const OpId id{range.first_op.value + i}; const auto& op = graph.ops[id.value];
      if (op.life != ValueLife::kLive) continue;
      const auto kind = BeforeLogicalFusion(graph, plan, id.value).kind;
      if (kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure ||
          std::holds_alternative<ValueJump>(op.operation) || std::holds_alternative<ValueNop>(op.operation)) {
        if (!InertSelectionOperation(op)) return false;
        continue;
      }
      if (child && (roots[id.value] == *child || prefix_owner[roots[id.value].value] == child)) continue;
      if (result && roots[id.value] == result->op &&
          BeforeLogicalFusion(graph, plan, result->op.value).kind == EvaluationKind::kPhiInput) continue;
      return false;
    }
    return true;
  };
  for (const auto block_id : order) {
    budget.Step(); const auto& block = graph.blocks[block_id.value];
    if (graph.phases[block_id.value].phase == ExceptionPhase::kCatch) continue;
    std::optional<EdgeId> first, second;
    for (std::uint32_t i = 0; i < block.edge_count; ++i) {
      budget.Step(); const EdgeId edge{block.first_edge.value + i};
      if (graph.edges[edge.value].kind == FlowEdgeKind::kException) continue;
      if (!first) first = edge; else if (!second) second = edge; else { first.reset(); break; }
    }
    if (!first) continue;
    if (!second) {
      std::optional<Path> path;
      if (inputs[first->value]) {
        const auto& use = graph.uses[inputs[first->value]->value];
        const auto* merged = std::get_if<ValueId>(&use.owner);
        const auto operand = ResolveEvaluationOperand(plan, use.operand);
        const auto* value = std::get_if<ValueId>(&operand);
        if (!merged || !value) continue;
        const auto* producer = std::get_if<OpResult>(&graph.values[value->value].origin);
        SelectionOperand selected = SelectionSnapshot{*value};
        if (producer && BeforeLogicalFusion(graph, plan, producer->op.value).kind == EvaluationKind::kPhiInput &&
            plan.operations[producer->op.value].consumer == inputs[first->value]) selected = *value;
        else if (!HasRetainedEvaluationValue(graph, plan, *value)) continue;
        path = Path{*merged, block_id, graph.edges[first->value].target, first, selected};
      } else path = paths[graph.edges[first->value].target.value];
      if (path && accepts(block_id, *path)) paths[block_id.value] = std::move(path);
      continue;
    }
    const auto& phase = graph.phases[block_id.value];
    if (phase.phase != ExceptionPhase::kCommit || !phase.operation) continue;
    const auto branch = *phase.operation; const auto& op = graph.ops[branch.value];
    if (!std::holds_alternative<ValueBranch>(op.operation) || op.use_count != 1) continue;
    const auto* condition = std::get_if<ValueId>(&graph.uses[op.first_use.value].operand);
    const auto* predicate = condition ? std::get_if<OpResult>(&graph.values[condition->value].origin) : nullptr;
    const auto yes = paths[graph.edges[first->value].target.value], no = paths[graph.edges[second->value].target.value];
    if (!predicate || !yes || !no || yes->value != no->value || yes->join != no->join ||
        (yes->incoming && yes->incoming == no->incoming) || (yes->child && yes->child == no->child)) continue;
    if ((yes->child && regions[*yes->child].parent) || (no->child && regions[*no->child].parent)) continue;
    // Unique original edge ownership makes the leaf count a coverage proof.
    // Reconvergent paths cannot count the same phi occurrence twice or claim
    // an unrelated incoming edge merely because the cardinalities agree.
    if ((yes->incoming && leaf_parents[yes->incoming->value]) ||
        (no->incoming && leaf_parents[no->incoming->value])) continue;
    const auto count = yes->leaves + no->leaves;
    const auto* phi = std::get_if<PhiDefinition>(&graph.values[yes->value.value].origin);
    if (!phi || phi->block != yes->join || count > leaves[yes->value.value] ||
        !graph.values[yes->value.value].domain.IsLanguage()) continue;
    SelectionRegion region{TreeLogical::kOr, predicate->op, branch, yes->value, *condition,
        no->operand, block_id, yes->tail, no->tail, yes->join, yes->incoming, no->incoming};
    region.consequent = yes->operand; region.complete_phi = count == leaves[yes->value.value];
    budget.Array(region.conditions, 1); region.conditions[0] = {branch, true};
    const Path closed{yes->value, block_id, yes->join, {}, SelectionChild{branch}, count, regions.size()};
    regions.push_back(std::move(region));
    if (!accepts(block_id, closed)) { regions.pop_back(); continue; }
    for (const auto* arm : {&*yes, &*no}) {
      if (arm->child) regions[*arm->child].parent = branch;
      else if (arm->incoming) leaf_parents[arm->incoming->value] = branch;
    }
    paths[block_id.value] = closed;
  }
  // Flat discovery remains the compatibility path. Return closed nested
  // families, retaining partial children solely under their actual parent.
  std::vector<std::uint8_t> kept; budget.Array(kept, regions.size());
  std::vector<std::optional<std::size_t>> branch_indices; budget.Array(branch_indices, graph.ops.size());
  for (std::size_t i = 0; i < regions.size(); ++i) { budget.Step(); branch_indices[regions[i].branch.value] = i; }
  for (std::size_t i = regions.size(); i-- > 0;) {
    budget.Step(); const auto& region = regions[i];
    if (region.parent) kept[i] = kept[*branch_indices[region.parent->value]];
    else kept[i] = region.complete_phi && (std::holds_alternative<SelectionChild>(region.right) ||
        std::holds_alternative<SelectionChild>(*region.consequent));
  }
  std::size_t count = 0;
  for (std::size_t i = 0; i < regions.size(); ++i) {
    budget.Step(); if (!kept[i]) continue;
    if (count != i) regions[count] = std::move(regions[i]);
    ++count;
  }
  regions.resize(count); return regions;
}
}  // namespace abcd::source
