#ifndef ABCD_SOURCE_MODULE_EVALUATION_H_
#define ABCD_SOURCE_MODULE_EVALUATION_H_

#include "evaluation_plan.h"

namespace abcd::source {
class TreeCheckBudget;
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
[[nodiscard]] std::variant<ModuleEvaluationIndex, EvaluationFailure>
IndexModuleEvaluations(const analysis::ValueProgram& graph,
    std::span<const ModuleEvaluation> modules, analysis::SemanticBudget budget = {});
// Conditional phased fusion: identical dispatch, unique load-Commit->guard
// corridor, and identical catch snapshots. Original pair/name/record proof
// remains mandatory at publication. No IR edges or staging writes are erased.
// Internal prerequisites: PhaseOrder follows the indexed pair shape checks;
// CatchCopies follows independent phase, module, phi and staging validation.
std::variant<std::uint64_t, EvaluationFailure> VerifyModulePhaseOrder(
    const analysis::ValueProgram& graph, std::span<const ModuleEvaluation> modules,
    analysis::SemanticBudget budget);
std::variant<std::uint64_t, EvaluationFailure> VerifyModuleCatchCopies(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget);
struct EvaluationUses {
  std::optional<analysis::UseId> first;
  bool multiple = false;
};
// Project verified observations: absorb the indexed guard dependency and, for
// a freshly verified normal LiteralPlan allocation, its initializer receivers.
// A member reference consumes its matching explicit VM receiver without a
// second source evaluation; mismatches and independent observers stay visible.
// Class projections consume the checked method/call/prototype uses as well.
// Fresh normal CellEvaluation witnesses also consume proved initialization
// checks; no parent/capture premise is invented by this use projection.
// Receiver protocol uses disappear only under the original guard obligation.
// One canonical-use census includes observations transported through proved
// copy/phi identities. O(I+U log(G+1)) work and O(I) result storage; no scan per
// producer or alias chain. Normal operands have identity canonicalization.
[[nodiscard]] std::vector<EvaluationUses> IndexEvaluationUses(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, const ModuleEvaluationIndex& modules,
    std::span<const std::uint8_t> literal_allocations,
    std::span<const std::optional<analysis::OpId>> class_projection,
    const CellEvaluation* initialized_cells, std::span<const analysis::OpId> receiver_guards,
    TreeCheckBudget& budget);

}  // namespace abcd::source
#endif
