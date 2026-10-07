#ifndef ABCD_SOURCE_FLOW_MOTION_H_
#define ABCD_SOURCE_FLOW_MOTION_H_
#include "recovery_flow.h"
namespace abcd::source {
struct FlowMotion {
  // The first observation or terminal sink reached after each point on EVERY path. Absence
  // includes disagreement and a possible
  // observation-free infinite path. Queries never scan a graph per candidate.
  std::vector<std::optional<FlowPoint>> next;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Backward universal data flow, seeded at reads, definitions, intrinsic effects
// and terminal sinks. All completion edges participate. Transparent cycles do
// not resolve, so may-reach is never mistaken for must-execute. O(N+E) time and
// O(N) space; only execution order is proved, not handler/activation equivalence.
[[nodiscard]] std::variant<FlowMotion,TreeStructureError> AnalyzeFlowMotion(
    const RecoveryFlow&,analysis::SemanticBudget = {});
}
#endif
