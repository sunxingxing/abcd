#ifndef ABCD_SOURCE_FLOW_MOTION_H_
#define ABCD_SOURCE_FLOW_MOTION_H_
#include "flow_liveness.h"
namespace abcd::source {
struct FlowMotion {
  // The first observation or terminal sink reached after each point on EVERY path. Absence
  // includes disagreement and a possible
  // observation-free infinite path. Queries never scan a graph per candidate.
  std::vector<std::optional<FlowPoint>> next;
  // Universal preceding observation over invocation-reachable paths. Entry
  // seeds invocation history; unreachable structural exits cannot add history.
  // Pairing next[write]==read with previous[read]==write prevents replay.
  std::vector<std::optional<FlowPoint>> previous;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Bidirectional universal data flow, seeded at reads, definitions, intrinsic effects
// and terminal sinks. All completion edges participate. Transparent cycles do
// not resolve, so may-reach is never mistaken for must-execute. O(N+E) time and
// O(N) space; only execution order is proved, not handler/activation equivalence.
[[nodiscard]] std::variant<FlowMotion,TreeStructureError> AnalyzeFlowMotion(
    const RecoveryFlow&,analysis::SemanticBudget = {});
// Current recovery consumes the same revision's invocation reachability.
// The standalone overload constructs its own facts for legacy callers only.
[[nodiscard]] std::variant<FlowMotion,TreeStructureError> AnalyzeFlowMotion(
    const RecoveryFlow&,const FlowLiveness&,analysis::SemanticBudget = {});
}
#endif
