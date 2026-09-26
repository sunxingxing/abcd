#ifndef ABCD_SOURCE_CELL_EVALUATION_H_
#define ABCD_SOURCE_CELL_EVALUATION_H_

#include "cell_initialization.h"

namespace abcd::source {
enum class IncomingCells : std::uint8_t { kReject, kConditional };
struct CellEvaluation {
  CellPlan cells;
  CellInitializations initialization;
  IncomingCells incoming = IncomingCells::kReject;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class CellEvaluationError : std::uint8_t {
  kEnvironment, kCells, kInitialization, kInternalIdentity, kResourceLimit, kWorkLimit
};
[[nodiscard]] bool IsEnvironmentState(const analysis::ValueOperation& operation);
[[nodiscard]] bool NeedsCellEvaluation(const analysis::ValueOperation& operation);
// Incoming cells default to rejection. Conditional mode retains their owner
// obligation for child-tree construction, NOT permission to publish. Neither
// mode permits observed VM environment identities or assumes child writes.
[[nodiscard]] std::variant<CellEvaluation, CellEvaluationError> BuildCellEvaluation(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {},
    IncomingCells incoming = IncomingCells::kReject);
[[nodiscard]] std::variant<std::uint64_t, CellEvaluationError> VerifyCellEvaluation(
    const analysis::ValueProgram& graph, const CellEvaluation& plan, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
