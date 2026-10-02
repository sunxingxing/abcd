#ifndef ABCD_SOURCE_CELL_DEFAULTS_H_
#define ABCD_SOURCE_CELL_DEFAULTS_H_

#include "cell_plan.h"
#include "parameter_plan.h"

namespace abcd::source {
struct CellDefault {
  std::uint32_t parameter;
  analysis::ValueId entry;
  CellId cell;
  analysis::OpId initial_store;
  analysis::OpId test_load;
  analysis::OpId comparison;
  analysis::OpId branch;
  analysis::OpId fallback_store;
  analysis::ValueOperand fallback;
  analysis::WorkBlockId header, arm, join;
  analysis::EdgeId present, missing, merge;
  std::optional<abc::PoolIndex> text;
};
struct CellDefaults {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<CellDefault> candidates;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class CellDefaultError : std::uint8_t {
  kRevision, kGraph, kParameters, kResourceLimit, kWorkLimit
};
// Discovery on unchanged verified graph/CellPlan/ParameterPlan. Walks only the
// entry default chain and stops at its first nonmatch, not at arbitrary body
// conditionals. O(ops + edges + formals), bounded indexed storage. No erasure,
// source initializer, escape proof, or signature/publication permission.
[[nodiscard]] std::variant<CellDefaults, CellDefaultError> FindCellDefaults(
    const analysis::ValueProgram& graph, const CellPlan& cells,
    const ParameterPlan& parameters, std::uint32_t length,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
