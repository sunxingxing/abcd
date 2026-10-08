#ifndef ABCD_SOURCE_DEFINITION_ELISION_H_
#define ABCD_SOURCE_DEFINITION_ELISION_H_
#include "flow_liveness.h"
namespace abcd::source {
struct FlowInitializations;
struct DefinitionWrite {
  FlowReferenceSite site;
  // GetValue remains at its original occurrence. A valueless shared-slot write
  // has no RHS evaluation; this does not denote lexical initialization.
  std::optional<ExprId> value;
  auto operator<=>(const DefinitionWrite&) const = default;
};
struct DefinitionElisions {
  std::vector<DefinitionWrite> writes;
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
struct InitialValueElision { StmtId statement; SymbolId binding; std::optional<ExprId> evaluation; };
struct InitialValueElisions {
  std::vector<InitialValueElision> values;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Delete an unused private initializer VALUE, keeping RHS effects and normal
// lexical completion at the same site. Source declaration spelling is separate.
[[nodiscard]] std::variant<InitialValueElisions,TreeStructureError> FindDeadInitialValues(
    const RecoveryFlow&,const FlowDemand&,analysis::SemanticBudget = {});
struct BindingElision {
  StmtId statement;
  // Retained RHS GetValue at the original declaration occurrence. Absence
  // means its entire evaluation region was proved unneeded and inert.
  std::optional<ExprId> value;
  std::optional<ExprId> write;
};
struct BindingElisions {
  std::vector<BindingElision> declarations;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Consume CURRENT retained syntax and value demand after dead-write projection.
// A surviving binding also retains references in its inert unused initializer;
// propagate these dependencies once per edge before removing any declaration.
// Required RHS evaluation stays. This does not move/share a retained lifetime.
[[nodiscard]] std::variant<BindingElisions,TreeStructureError> FindUnusedBindings(
    const RecoveryFlow&,const FlowDemand&,analysis::SemanticBudget = {});
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
// With fixed retained roots, removing a mapping leaves every other member's
// action valid: original liveness and completion proofs are member-local.
[[nodiscard]] std::variant<StorageInitializations,TreeStructureError> PlanStorageInitializations(
    const RecoveryFlow&,const FlowLiveness&,const FlowInitializations&,std::span<const std::optional<SymbolId>>,
    analysis::SemanticBudget = {});
}
#endif
