#ifndef ABCD_SOURCE_DEFINITION_ELISION_H_
#define ABCD_SOURCE_DEFINITION_ELISION_H_
#include "flow_liveness.h"
namespace abcd::source {
struct FlowInitializations;
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
struct BindingElisions {
  std::vector<StmtId> declarations;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Separate lifetime decision against the SAME immutable flow and proposed delta.
// Remove an undefined initialization only if its private binding has no remaining
// reference or other definition. This does not authorize moving/sharing a live
// lifetime, or dropping native/explicit initializer evaluation.
[[nodiscard]] std::variant<BindingElisions,TreeStructureError> PlanBindingElisions(
    const RecoveryFlow&,const DefinitionElisions&,analysis::SemanticBudget = {});
struct StorageInitializations {
  std::vector<std::optional<SymbolId>> stores;
  std::vector<std::uint8_t> blocked;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Separately preserve live undefined values after proposed storage sharing.
// Shared liveness decides value retention; completion dominance proves that
// the retained declaration has completed before the preserved write. Source
// scope/name availability remains the source binding planner's obligation.
// The initialization facts are shared with reference checking on this same
// unchanged flow; this planner neither rebuilds them nor applies the mapping.
[[nodiscard]] std::variant<StorageInitializations,TreeStructureError> PlanStorageInitializations(
    const RecoveryFlow&,const FlowLiveness&,const FlowInitializations&,std::span<const std::optional<SymbolId>>,
    analysis::SemanticBudget = {});
}
#endif
