#ifndef ABCD_SOURCE_RECOVERY_PROJECTION_H_
#define ABCD_SOURCE_RECOVERY_PROJECTION_H_
#include "binding_plan_view.h"
#include "flow_constants.h"
#include "flow_dominance.h"
namespace abcd::source {
struct RecoveryExpressions {
  std::vector<std::optional<SymbolId>> bindings;
  std::vector<std::optional<PrimitiveId>> literals;
  std::vector<ExprId> roots;
  std::vector<std::uint8_t> normal_stores;
  std::vector<ExpressionEvaluation> evaluations;
  std::vector<LexicalDependency> lexical;
  std::vector<analysis::ValueKinds> kinds;
};
// Borrow one complete, unchanged proposal during synchronous analysis. The
// binding/declaration/value plan is read-only; owned candidate overlays replace
// its spans before any query. Canonical equations belong to preparation.
// No trial accepts another projection's occurrence-initialization proofs.
struct RecoveryExpressionInput {
  BindingPlanView bindings;
  std::span<const FlowValueEquation> equations;
  std::span<const ExprId> postorder;
  const OriginalFlow* original=nullptr;
  // Exact-projection normal domains from the shared success-definition solver.
  // Internal expression-analysis input only. PrepareRecoveryProjection rejects
  // a supplied span, computes its own facts, and never borrows a prior plan.
  std::span<const analysis::ValueKinds> normal_kinds;
};
[[nodiscard]] FlowValueEquation ProjectedRecoveryEquation(const RecoveryExpressionInput&,ExprId);
[[nodiscard]] bool WritableRecoveryBinding(const FunctionTree&,SymbolId);
// Pure facts, not a plan updater. Original normal-result facts are replaced by
// the actual proposal producer; effects and initialization remain independent.
[[nodiscard]] RecoveryExpressions AnalyzeRecoveryExpressions(const FunctionTree&,
    const RecoveryExpressionInput&,std::span<const std::uint8_t> initialized,TreeCheckBudget&);
struct RecoveryProjection {
  RecoveryExpressions expressions;
  RecoveryFlow flow;
  std::vector<std::uint8_t> initialized_reads;
  // Conservative maximum of admitted simultaneous storage across bootstrap
  // phases. Work includes all phases; freed scratch is not retained storage.
  std::size_t allocated_bytes=0;
  // Conservative owned storage after solver/order/initialization scratch has
  // been destroyed. Cache clients first admit the peak above, then retain
  // only this charge; current and candidate projections have the same owner.
  std::size_t retained_bytes=0;
  std::uint64_t work=0;
};
// Common provider: conservative proposal -> shared initialization equations
// and sparse value facts -> joint executable-completion solve -> final effects.
// Both current and candidate plans use it, with or without original IR.
// Raw-proof invalidation participates in the same completion dependencies.
// The returned facts/flow describe ONLY this
// proposal. Scope/handler/provenance use the same adapter as committed passes;
// preparation must not publish them as facts of another revision.
[[nodiscard]] std::variant<RecoveryProjection,TreeStructureError> PrepareRecoveryProjection(
    const FunctionTree&,LegacyFlowProjection,const RecoveryExpressionInput&,
    analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
