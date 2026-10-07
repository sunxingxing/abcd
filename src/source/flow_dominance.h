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
// Binary fanout is only an adapter for the existing LT implementation; it must
// not change paths between real events or become a recovery admission pattern.
[[nodiscard]] std::variant<FlowDominance,TreeStructureError> AnalyzeFlowDominance(
    const RecoveryFlow&,analysis::SemanticBudget = {});
}
#endif
