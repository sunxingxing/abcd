#ifndef ABCD_SOURCE_FLOW_CYCLES_H_
#define ABCD_SOURCE_FLOW_CYCLES_H_
#include "recovery_flow.h"
namespace abcd::source {
struct FlowCycles {
  std::vector<std::uint8_t> cyclic;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Iterative Kosaraju on every retained completion edge. A cyclic point may
// repeat; absence of a graph cycle proves finite traversal only, never that
// calls/coercions/suspension are inert. No per-region or per-binding graph walk.
[[nodiscard]] std::variant<FlowCycles,TreeStructureError> AnalyzeFlowCycles(
    const RecoveryFlow&,analysis::SemanticBudget = {});
}
#endif
