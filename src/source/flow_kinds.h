#ifndef ABCD_SOURCE_FLOW_KINDS_H_
#define ABCD_SOURCE_FLOW_KINDS_H_
#include "flow_liveness.h"
namespace abcd::source {
struct RecoveryExpressionInput;
struct FlowNormalKinds {
  std::vector<analysis::ValueKinds> expressions;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Sparse normal-value equations over this exact projected graph. Successful
// writes and unchanged original evaluation intervals generate domains;
// throwing paths retain the previous state. Static
// definition identity never equates values across executions or scope reentry.
[[nodiscard]] std::variant<FlowNormalKinds,TreeStructureError> AnalyzeFlowNormalKinds(
    const FunctionTree&,const RecoveryFlow&,const FlowLiveness&,
    const RecoveryExpressionInput&,analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
