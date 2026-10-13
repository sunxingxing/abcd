#include "selection_consumer.h"
#include "tree_checks.h"
namespace abcd::source {
using namespace analysis;
SelectionConsumer FindSelectionConsumer(const ValueProgram& graph, const SelectionRegion& region,
    TreeCheckBudget& budget, const SelectionRegion* logical_left) {
  SelectionConsumer result;
  const auto refuse = [&](SelectionExitReason reason, UseId witness) {
    result.kind = reason == SelectionExitReason::kSnapshot ? SelectionExitKind::kSnapshot : SelectionExitKind::kMaterialized;
    result.reason = reason; result.witness = witness;
    const auto& occurrence = graph.uses[witness.value];
    if (occurrence.incoming) if (const auto* edge = std::get_if<EdgeId>(&*occurrence.incoming)) result.edge = *edge;
    return result;
  };
  for (auto use = graph.values[region.merged.value].first_use; use; use = graph.uses[use->value].next) {
    budget.Step(); const auto& occurrence = graph.uses[use->value];
    if (occurrence.life != ValueLife::kLive) continue;
    if (logical_left && occurrence.owner == decltype(occurrence.owner){logical_left->merged}) {
      const auto* edge = occurrence.incoming ? std::get_if<EdgeId>(&*occurrence.incoming) : nullptr;
      if (result.keep || !edge || logical_left->keep_input != *edge || occurrence.role != OperandRole::kPhiInput)
        return refuse(SelectionExitReason::kKeepEdge, *use);
      result.keep = use; result.edge = *edge; continue;
    }
    const auto* owner = std::get_if<OpId>(&occurrence.owner);
    if (!owner) return refuse(SelectionExitReason::kPhiUse, *use);
    if (result.use) return refuse(SelectionExitReason::kMultipleUses, *use);
    if (graph.ops[owner->value].block != region.join) return refuse(SelectionExitReason::kCrossBlock, *use);
    if (logical_left && *use != graph.ops[logical_left->boolean.value].first_use)
      return refuse(SelectionExitReason::kProtocolRole, *use);
    const auto shape = DescribeEvaluation(graph.ops[owner->value].operation);
    if (!shape || occurrence.role == OperandRole::kReceiver) return refuse(SelectionExitReason::kProtocolRole, *use);
    if (shape->NeedsSnapshot(occurrence.role) || PredicateSnapshot(graph, graph.ops[owner->value], occurrence.operand))
      return refuse(SelectionExitReason::kSnapshot, *use);
    result.use = use;
  }
  if (logical_left && !result.keep) { result.reason = SelectionExitReason::kKeepEdge; return result; }
  if (result.use) {
    result.kind = result.keep ? SelectionExitKind::kPredicateKeep : SelectionExitKind::kUnique;
    result.reason = SelectionExitReason::kNone;
  }
  return result;
}
}
