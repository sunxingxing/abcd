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
// Normal-completion copy metadata generates relations, inheriting proved entry
// equality through private copies. Common liveness includes source-value queries
// at copy commit; missing query facts remain unknown. Every entry-root definition
// kills its relations. Observable scoped roots consume the common unit transfer
// on every edge, including generation, so departure/reallocation/unknown cleanup
// cannot identify an older saved value with a new cell. Completion-specific user
// effects also kill exposed roots. Entry-cell evidence still excludes mapped/
// captured/signature mutation. Private carriers inherit roots; this lattice does
// not give an unknown carrier an immutable root. Initialization, lexical names,
// effects and deletion are independent. No per-root graph walk is performed.
[[nodiscard]] std::variant<FlowCopies,TreeStructureError> AnalyzeFlowCopies(
    const RecoveryFlow&,const FlowLiveness&,FlowValueEquations,
    analysis::SemanticBudget = {});
}
#endif
