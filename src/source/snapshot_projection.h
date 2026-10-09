#ifndef ABCD_SOURCE_SNAPSHOT_PROJECTION_H_
#define ABCD_SOURCE_SNAPSHOT_PROJECTION_H_
#include "binding_plan_view.h"
#include "source_name_retirement.h"
namespace abcd::source {
enum class SnapshotSelection : std::uint8_t {kRetain,kForward};
struct SnapshotMove {
  SymbolId binding;
  StmtId declaration;
  ExprId read,value;
  std::optional<ExprId> expression;
  StmtId statement;
  std::optional<StmtId> initialization;
};
// Own every changed semantic facet for one immutable transaction. Borrowed
// unchanged facets remain in the parent; neither trial nor commit may invent
// another retirement policy. Motion/name/unit proofs belong to the caller.
struct SnapshotProjection {
  std::vector<std::optional<BindingInitializer>> statements;
  std::vector<std::uint8_t> omitted;
  std::vector<std::optional<DeclarationValue>> values;
  std::vector<std::optional<DeclarationCompletion>> completions;
  std::vector<std::optional<ExprId>> replacements,forwarded_values;
  std::vector<std::optional<ExpressionTransport>> transports;
  std::vector<std::optional<SymbolId>> transported_reads;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
  [[nodiscard]] BindingPlanView View(BindingPlanView parent) const;
  void Publish(BindingInitializers&) &&;
};
[[nodiscard]] std::variant<SnapshotProjection,TreeStructureError> PlanSnapshotProjection(
    BindingPlanView,std::span<const SnapshotMove>,std::span<const SnapshotSelection>,
    PrimitiveId undefined,analysis::SemanticBudget = {});
struct SnapshotRetention {
  std::vector<SnapshotSelection> selections;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Current scopes describe restored bindings. Draft owners describe moved reads.
// Indexed name retirement closes restoration dependencies and nested transport
// components; it does not infer value availability, motion or cell identity.
[[nodiscard]] std::variant<SnapshotRetention,TreeStructureError> PlanSnapshotRetention(
    const RecoveryFlow& current,const RecoveryFlow& draft,std::span<const SnapshotMove>,
    std::span<const SourceRetentionDemand>,analysis::SemanticBudget = {});

}
#endif
