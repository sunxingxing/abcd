#ifndef ABCD_SOURCE_DEFINITION_ELISION_H_
#define ABCD_SOURCE_DEFINITION_ELISION_H_
#include "flow_liveness.h"
namespace abcd::source {
struct DefinitionElisions {
  std::vector<ExprId> writes;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Consumes the exact unchanged flow/liveness pair. Removes only dead inert
// PutValue operations, never RHS evaluation, initialization or binding lifetime.
[[nodiscard]] std::variant<DefinitionElisions,TreeStructureError> PlanDefinitionElisions(
    const RecoveryFlow&,const FlowLiveness&,analysis::SemanticBudget = {});
}
#endif
