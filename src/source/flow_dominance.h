#ifndef ABCD_SOURCE_FLOW_DOMINANCE_H_
#define ABCD_SOURCE_FLOW_DOMINANCE_H_
#include "recovery_flow.h"
#include "abcd/analysis/dominators.h"
namespace abcd::source {
struct FlowDominance {
  analysis::DominatorTree tree;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
  [[nodiscard]] bool Dominates(FlowPoint before,FlowPoint after) const {
    return tree.Dominates({before.value},{after.value});
  }
};
// Include EVERY completion edge, including exceptions and conservative cleanup.
// CSR adjacency feeds the existing LT implementation directly. Arbitrary
// fanout needs no synthetic events and never becomes a recovery admission.
[[nodiscard]] std::variant<FlowDominance,TreeStructureError> AnalyzeFlowDominance(
    const RecoveryFlow&,analysis::SemanticBudget = {});
struct FlowInitializations {
  FlowDominance order;
  std::shared_ptr<const FlowScopes> scopes;
  std::vector<std::optional<ScopeId>> units;
  std::vector<std::optional<FlowPoint>> declarations;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
  // Completion order includes canonical allocation-before-initialization.
  // Projected scope and cell equality remain independent; this query cannot
  // authorize lifetime movement alone.
  // Unreachable retained syntax has no execution-order demand; lexical name
  // resolution is still checked independently for that occurrence.
  [[nodiscard]] bool Completed(SymbolId binding,FlowPoint occurrence) const {
    if(!order.tree.nodes[occurrence.value].begin)return true;
    if(scopes&&units[binding.value]) {
      const auto presence=scopes->Presence(*units[binding.value],occurrence,FlowScopePhase::kBefore);
      if(presence!=FlowUnitPresence::kActive&&presence!=FlowUnitPresence::kCompletionOnly)return false;
    }
    return declarations[binding.value] && order.Dominates(*declarations[binding.value],occurrence);
  }
};
[[nodiscard]] std::variant<FlowInitializations,TreeStructureError> AnalyzeFlowInitializations(
    const RecoveryFlow&,analysis::SemanticBudget = {});
}
#endif
