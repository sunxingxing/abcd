#ifndef ABCD_SOURCE_FLOW_COPIES_H_
#define ABCD_SOURCE_FLOW_COPIES_H_
#include "flow_constants.h"
namespace abcd::source {
struct FlowReadCopy {ExprId expression;SymbolId source;};
struct FlowCopies {
  // At this completed read, the private stored value equals the source's
  // current value. This is neither an immutable value nor a storage mapping.
  std::vector<FlowReadCopy> reads;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Classical available-copy gen/kill on shared sparse incoming live facts.
// Normal-completion copy metadata generates relations; every source definition
// kills them. All completion edges participate. Entry-cell adapter evidence
// excludes mapped/captured/signature mutation; local activation roots remain
// unproved. Initialization, lexical names, effects and deletion are independent.
[[nodiscard]] std::variant<FlowCopies,TreeStructureError> AnalyzeFlowCopies(
    const RecoveryFlow&,const FlowLiveness&,FlowValueEquations,
    analysis::SemanticBudget = {});
}
#endif
