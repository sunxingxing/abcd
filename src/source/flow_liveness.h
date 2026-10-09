#ifndef ABCD_SOURCE_FLOW_LIVENESS_H_
#define ABCD_SOURCE_FLOW_LIVENESS_H_
#include "recovery_flow.h"
namespace abcd::source {
struct LiveBinding {
  FlowPoint point;
  SymbolId binding;
  auto operator<=>(const LiveBinding&) const = default;
};
struct FlowLiveness {
  // Sorted incoming facts for retained reads and normal-copy value queries.
  // Both refer to state before a definition at the same point; a proof query
  // is not an observable read and never seeds FlowDemand.
  std::vector<LiveBinding> live_in;
  // A demanded incoming state crosses a definite exit or unknown lifetime.
  // Unknown transfer still retains definitions; absence never proves a value.
  std::vector<LiveBinding> unavailable;
  std::vector<std::uint8_t> live_definitions;
  // Invocation reachability includes every completion edge. Liveness remains
  // conservative for retained syntax; contextual values consume this separately.
  std::vector<std::uint8_t> reachable;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Demand uses a different result type: its selected roots must not be mistaken
// for all-read liveness by storage interference or reaching-definition clients.
struct FlowDemand {
  std::vector<std::uint8_t> definitions, retained;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
[[nodiscard]] std::variant<FlowDemand,TreeStructureError> AnalyzeFlowDemand(
    const RecoveryFlow&,analysis::SemanticBudget = {});
// One shared sparse worklist; no per-binding graph reconstruction. A live
// definition is not permission to move storage, and a dead definition is not
// permission to remove its RHS effects or its lexical initialization.
[[nodiscard]] std::variant<FlowLiveness,TreeStructureError> AnalyzeFlowLiveness(
    const RecoveryFlow&,analysis::SemanticBudget = {});
}
#endif
