#ifndef ABCD_SOURCE_MODULE_EVALUATION_H_
#define ABCD_SOURCE_MODULE_EVALUATION_H_

#include "evaluation_plan.h"

namespace abcd::source {
struct ModuleEvaluationIndex {
  // Symmetric read<->guard lookup; empty for all other operations.
  std::vector<std::optional<analysis::OpId>> peers;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Validate exact live-read census and normal adjacency or phased pair order.
// This checks conditional evaluation structure, NOT original proof/record identity.
// Guards retain their exceptional effects. Normal-flow reads stay pinned;
// phased expression composition may move the proven pair as one effect subtree.
[[nodiscard]] std::variant<ModuleEvaluationIndex, EvaluationError>
IndexModuleEvaluations(const analysis::ValueProgram& graph,
    std::span<const ModuleEvaluation> modules, analysis::SemanticBudget budget = {});
// Conditional phased fusion: identical dispatch, unique load-Commit->guard
// corridor, and identical catch snapshots. Original pair/name/record proof
// remains mandatory at publication. No IR edges or staging writes are erased.
// Internal prerequisites: PhaseOrder follows the indexed pair shape checks;
// CatchCopies follows independent phase, module, phi and staging validation.
std::variant<std::uint64_t, EvaluationError> VerifyModulePhaseOrder(
    const analysis::ValueProgram& graph, std::span<const ModuleEvaluation> modules,
    analysis::SemanticBudget budget);
std::variant<std::uint64_t, EvaluationError> VerifyModuleCatchCopies(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget);
struct EvaluationUses {
  std::optional<analysis::UseId> first;
  bool multiple = false;
};
// Project a verified use chain: absorb the indexed guard dependency and, for
// a freshly verified normal LiteralPlan allocation, its initializer receivers.
// O(1) without initializer projection; otherwise O(number of allocation uses).
// Cache once per producer, never rescan a shared producer for every consumer.
[[nodiscard]] EvaluationUses ProjectEvaluationUses(const analysis::ValueProgram& graph,
    analysis::OpId operation, const ModuleEvaluationIndex& modules,
    bool project_initializers = false);
}  // namespace abcd::source
#endif
