#ifndef ABCD_SOURCE_CELL_INITIALIZATION_H_
#define ABCD_SOURCE_CELL_INITIALIZATION_H_

#include "cell_plan.h"
#include "abcd/analysis/semantic_validation.h"

namespace abcd::source {
struct NotCellRead {};
struct IncomingInitialization {};
struct StoredInitialization { analysis::OpId store; };
using CellReadInitialization = std::variant<NotCellRead, IncomingInitialization, StoredInitialization>;
struct CellInitializations {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  analysis::ClosureWrites closure_writes;
  std::vector<CellReadInitialization> reads;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class CellInitializationError : std::uint8_t {
  kRevision, kCells, kDomain, kClosure, kUninitialized, kWitness, kResourceLimit, kWorkLimit
};
// Dominating-store witnesses of initialization, NEVER reaching values or const
// snapshots. Local stores must remain Language. kLanguage is an explicit owner
// premise requiring complete transitive child-write validation at publication.
// Unknown creation writes conservatively refuse; incoming reads retain a parent
// obligation. Joins requiring initialization phis remain a separate extension.
[[nodiscard]] std::variant<CellInitializations, CellInitializationError> BuildCellInitializations(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, analysis::ClosureWrites writes = analysis::ClosureWrites::kUnknown,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, CellInitializationError> VerifyCellInitializations(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, analysis::ClosureWrites writes, const CellInitializations& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
