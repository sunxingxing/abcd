#ifndef ABCD_SOURCE_DEFINITION_ELISION_H_
#define ABCD_SOURCE_DEFINITION_ELISION_H_
#include "flow_liveness.h"
namespace abcd::source {
struct DefinitionElisions {
  std::vector<ExprId> writes;
  std::vector<StmtId> statements;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Consumes demand facts from this exact unchanged flow. The shared sparse
// liveness solver closes dead-copy cascades before any delta is applied.
// Effectful RHSs stay; discarded inert evaluation may disappear as a whole.
// Initialization and binding lifetime are never removed here.
[[nodiscard]] std::variant<DefinitionElisions,TreeStructureError> PlanDefinitionElisions(
    const RecoveryFlow&,const FlowDemand&,analysis::SemanticBudget = {});
}
#endif
