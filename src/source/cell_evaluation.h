#ifndef ABCD_SOURCE_CELL_EVALUATION_H_
#define ABCD_SOURCE_CELL_EVALUATION_H_

#include "cell_initialization.h"

namespace abcd::source {
struct CellEvaluation {
  CellPlan cells;
  CellInitializations initialization;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class CellEvaluationError : std::uint8_t {
  kEnvironment, kCells, kInitialization, kInternalIdentity, kResourceLimit, kWorkLimit
};
[[nodiscard]] bool IsEnvironmentState(const analysis::ValueOperation& operation);
[[nodiscard]] bool NeedsCellEvaluation(const analysis::ValueOperation& operation);
// First native-cell admission: no incoming captures or observed VM environment
// identities. Shared cells remain ordered memory operations. Complete child
// publication/capture obligations are not assumed by this entry point.
[[nodiscard]] std::variant<CellEvaluation, CellEvaluationError> BuildCellEvaluation(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, CellEvaluationError> VerifyCellEvaluation(
    const analysis::ValueProgram& graph, const CellEvaluation& plan, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
