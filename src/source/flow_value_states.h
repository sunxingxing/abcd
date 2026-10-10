#ifndef ABCD_SOURCE_FLOW_VALUE_STATES_H_
#define ABCD_SOURCE_FLOW_VALUE_STATES_H_
#include "flow_liveness.h"
namespace abcd::source {
// Sparse evaluation states, not storage liveness. A forwarding state keeps
// its own last completion guard; its source skips only unchanged unit/value
// transport. Joins, successful writes and callback clobbers remain explicit.
struct FlowValueStates {
  const RecoveryFlow* owner=nullptr;
  std::vector<LiveBinding> states;
  std::vector<std::optional<LiveBinding>> forwarded;
  std::vector<std::uint8_t> reachable;
  std::size_t allocated_bytes=0,retained_bytes=0;
  std::uint64_t work=0;
};
[[nodiscard]] std::variant<FlowValueStates,TreeStructureError> BuildFlowValueStates(
    const RecoveryFlow&,analysis::SemanticBudget = {});
}
#endif
