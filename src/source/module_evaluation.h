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
// Validate exact live-read census and same-block pair shape in O(I+M). This
// checks conditional evaluation structure, NOT original proof/record identity.
// Guards retain their exceptional effects, and reads stay pinned at the pair.
[[nodiscard]] std::variant<ModuleEvaluationIndex, EvaluationError>
IndexModuleEvaluations(const analysis::ValueProgram& graph,
    std::span<const ModuleEvaluation> modules, analysis::SemanticBudget budget = {});
struct EvaluationUses {
  std::optional<analysis::UseId> first;
  bool multiple = false;
};
// O(1) projection of a verified use chain. Only the indexed paired guard's
// single dependency is absorbed; all real value uses still require snapshots.
[[nodiscard]] EvaluationUses ProjectEvaluationUses(const analysis::ValueProgram& graph,
    analysis::OpId operation, const ModuleEvaluationIndex& modules);
}  // namespace abcd::source
#endif
