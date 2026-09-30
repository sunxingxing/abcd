#include "logical_regions.h"
#include "evaluation_plan.h"
#include "tree_checks.h"

#include <array>

namespace abcd::source {
namespace {
using namespace analysis;
struct BlockSummary {
  std::optional<OpId> last;
  std::optional<OpId> previous;
  std::optional<ValueId> phi;
  bool multiple_phis = false;
  bool empty = true;
};
struct PhiInputs {
  std::array<UseId, 2> uses{};
  unsigned count = 0;
};
struct Arm {
  std::optional<WorkBlockId> block;
  EdgeId incoming;
};
bool SameValue(const ValueOperand& operand, ValueId expected) {
  const auto* value = std::get_if<ValueId>(&operand);
  return value && *value == expected;
}
bool Pure(const ValueOp& op) {
  return op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
      op.exceptional == ExceptionalCompletion::kImpossible;
}
// The ControlPlan owns single-entry region membership. A straight arm may
// end at the common join, but may not contain a nested branch/loop/transfer.
std::optional<Arm> ReadArm(const ValueProgram& graph, const ControlPlan& control,
    const std::optional<ControlId>& id, WorkBlockId join, EdgeId entry) {
  if (!id) {
    if (graph.edges[entry.value].target == join) return Arm{{}, entry};
    return {};
  }
  const auto& node = control.nodes[id->value];
  if (node.next || node.arms || !std::holds_alternative<std::monostate>(node.synthetic)) return {};
  const auto& block = graph.blocks[node.block.value];
  if (graph.edges[entry.value].target != node.block || block.edge_count != 1 ||
      graph.edges[block.first_edge.value].target != join) return {};
  return Arm{node.block, block.first_edge};
}
}  // namespace
std::vector<LogicalRegion> FindLogicalRegions(const ValueProgram& graph, const ControlPlan& control, TreeCheckBudget* budget) {
  std::vector<LogicalRegion> result;
  if (!graph.phases.empty() || control.view != ControlViewKind::kNormal ||
      graph.revision != control.revision || graph.cfg_revision != control.cfg_revision) return result;
  std::vector<BlockSummary> blocks;
  std::vector<PhiInputs> phis;
  std::vector<unsigned> use_counts;
  if (budget) {
    budget->Array(blocks, graph.blocks.size()); budget->Array(phis, graph.values.size());
    budget->Array(use_counts, graph.values.size()); budget->Admit(control.nodes.size(), sizeof(LogicalRegion));
  } else {
    blocks.resize(graph.blocks.size()); phis.resize(graph.values.size()); use_counts.resize(graph.values.size());
  }
  result.reserve(control.nodes.size());
  if (budget) {
    budget->Admit(result.capacity() - control.nodes.size(), sizeof(LogicalRegion));
    budget->Step(graph.ops.size()); budget->Step(graph.values.size()); budget->Step(graph.uses.size());
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    const auto& op = graph.ops[i];
    if (op.life != ValueLife::kLive) continue;
    auto& block = blocks[op.block.value];
    block.previous = block.last; block.last = OpId{static_cast<std::uint32_t>(i)};
    if ((!std::holds_alternative<ValueNop>(op.operation) &&
         !std::holds_alternative<ValueJump>(op.operation)) || !Pure(op)) block.empty = false;
  }
  for (std::size_t i = 0; i < graph.values.size(); ++i) {
    const auto& value = graph.values[i];
    if (value.life != ValueLife::kLive) continue;
    if (const auto* phi = std::get_if<PhiDefinition>(&value.origin)) {
      auto& block = blocks[phi->block.value];
      if (block.phi) block.multiple_phis = true;
      else block.phi = ValueId{static_cast<std::uint32_t>(i)};
    }
  }
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    const auto& use = graph.uses[i];
    if (use.life != ValueLife::kLive) continue;
    if (const auto* value = std::get_if<ValueId>(&use.operand)) {
      // Saturation suffices: only single-use predicate ownership matters here.
      auto& count = use_counts[value->value]; if (count < 2) ++count;
    }
    if (const auto* owner = std::get_if<ValueId>(&use.owner)) {
      auto& inputs = phis[owner->value];
      if (inputs.count < inputs.uses.size()) inputs.uses[inputs.count] = UseId{static_cast<std::uint32_t>(i)};
      if (inputs.count <= inputs.uses.size()) ++inputs.count;
    }
  }
  for (const auto& node : control.nodes) {
    if (budget) budget->Step();
    if (!node.arms || !node.next || !std::holds_alternative<std::monostate>(node.synthetic)) continue;
    const auto& header = graph.blocks[node.block.value];
    const auto& summary = blocks[node.block.value];
    if (header.edge_count != 2 || !summary.last || !summary.previous) continue;
    const auto branch_id = *summary.last, boolean_id = *summary.previous;
    const auto& branch_op = graph.ops[branch_id.value];
    const auto& boolean_op = graph.ops[boolean_id.value];
    const auto* branch = std::get_if<ValueBranch>(&branch_op.operation);
    const auto* boolean = std::get_if<ValueUnary>(&boolean_op.operation);
    if (!branch || !boolean || boolean->op != UnaryOperator::kToBoolean ||
        !Pure(branch_op) || !Pure(boolean_op) || !boolean_op.result ||
        branch_op.use_count != 1 || boolean_op.use_count != 1 ||
        use_counts[boolean_op.result->value] != 1 ||
        !SameValue(graph.uses[branch_op.first_use.value].operand, *boolean_op.result)) continue;
    const bool taken_truthy = branch->predicate == UnaryOperator::kTrueOrNonzeroNumber;
    if (!taken_truthy && branch->predicate != UnaryOperator::kFalseOrZero) continue;
    const auto* left = std::get_if<ValueId>(&graph.uses[boolean_op.first_use.value].operand);
    if (!left || !graph.values[left->value].domain.IsLanguage()) continue;
    const auto& join_node = control.nodes[node.next->value];
    if (!std::holds_alternative<std::monostate>(join_node.synthetic)) continue;
    const auto join = join_node.block;
    const auto& merged = blocks[join.value];
    if (!merged.phi || merged.multiple_phis || phis[merged.phi->value].count != 2 ||
        !graph.values[merged.phi->value].domain.IsLanguage()) continue;
    const auto taken = ReadArm(graph, control, node.arms->taken, join, header.first_edge);
    const auto fallthrough = ReadArm(graph, control, node.arms->fallthrough, join, {header.first_edge.value + 1});
    if (!taken || !fallthrough) continue;
    const std::array<Arm, 2> arms{*taken, *fallthrough};
    for (unsigned keep = 0; keep < arms.size(); ++keep) {
      const auto& retained = arms[keep]; const auto& evaluated = arms[1 - keep];
      if ((retained.block && (!blocks[retained.block->value].empty || blocks[retained.block->value].phi)) ||
          !evaluated.block || blocks[evaluated.block->value].phi) continue;
      const OperandUse* keep_use = nullptr; const OperandUse* evaluate_use = nullptr;
      for (const auto id : phis[merged.phi->value].uses) {
        const auto& use = graph.uses[id.value];
        if (!use.incoming) continue;
        const auto* edge = std::get_if<EdgeId>(&*use.incoming);
        if (!edge) continue;
        if (*edge == retained.incoming) keep_use = &use;
        if (*edge == evaluated.incoming) evaluate_use = &use;
      }
      if (!keep_use || !evaluate_use || !SameValue(keep_use->operand, *left)) continue;
      const bool keep_truthy = keep == 0 ? taken_truthy : !taken_truthy;
      result.push_back({keep_truthy ? TreeLogical::kOr : TreeLogical::kAnd,
          boolean_id, branch_id, *merged.phi, *left, evaluate_use->operand,
          node.block, retained.block, *evaluated.block, join, retained.incoming, evaluated.incoming});
      break;
    }
  }
  return result;
}
std::vector<LogicalRegion> FindLogicalExpressions(const ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, TreeCheckBudget& budget) {
  if (evaluation.flow != EvaluationFlow::kNormal || evaluation.operations.size() != graph.ops.size() ||
      evaluation.revision != graph.revision || evaluation.cfg_revision != graph.cfg_revision) return {};
  auto regions = FindLogicalRegions(graph, control, &budget);
  std::size_t count = 0;
  for (const auto& region : regions) {
    budget.Step();
    const auto& boolean = evaluation.operations[region.boolean.value];
    if (boolean.kind != EvaluationKind::kInline ||
        boolean.consumer != graph.ops[region.branch.value].first_use) continue;
    const auto* right = std::get_if<ValueId>(&region.right);
    const auto* producer = right ? std::get_if<OpResult>(&graph.values[right->value].origin) : nullptr;
    if (!producer || graph.ops[producer->op.value].block != region.evaluate ||
        evaluation.operations[producer->op.value].kind != EvaluationKind::kPhiInput) continue;
    const auto& block = graph.blocks[region.evaluate.value];
    bool closed = true;
    for (std::uint32_t j = 0; j < block.op_count; ++j) {
      budget.Step(); const auto id = block.first_op.value + j; const auto& op = graph.ops[id];
      if (op.life != ValueLife::kLive || std::holds_alternative<ValueJump>(op.operation)) continue;
      if (evaluation.operations[id].kind != EvaluationKind::kInline && id != producer->op.value) closed = false;
    }
    if (region.keep) {
      const auto& keep = graph.blocks[region.keep->value];
      for (std::uint32_t j = 0; j < keep.op_count; ++j) {
        budget.Step(); const auto& op = graph.ops[keep.first_op.value + j];
        if (op.life == ValueLife::kLive && !std::holds_alternative<ValueJump>(op.operation)) closed = false;
      }
    }
    if (closed) regions[count++] = region;
  }
  regions.resize(count); return regions;
}
}  // namespace abcd::source
