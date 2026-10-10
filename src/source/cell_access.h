#ifndef ABCD_SOURCE_CELL_ACCESS_H_
#define ABCD_SOURCE_CELL_ACCESS_H_
#include "tree_checks.h"
#include <span>
namespace abcd::source {
// Whether user code outside this function can reach and change a binding.
// Unknown capability is conservative; it is not a language scope invariant.
enum class CellVisibility : std::uint8_t {kClosed,kExternal,kUnmodeled};
struct CellCapture {ExprId creation;SymbolId binding;};
// Canonical source semantics, borrowed by current/trial projections of this
// unchanged owner. The source tree and prepared class bodies must remain
// immutable for its lifetime; plan revisions only change the projection.
// External access is independent of writes, stored values,
// initialization and allocation identity. Calls cannot acquire direct eval;
// lexical captures/modules and mapped arguments remain externally accessible.
struct SourceCells {
  const FunctionTree* owner=nullptr;
  std::vector<CellVisibility> access;
  std::vector<CellCapture> captures; // ordered by creation, not arena execution
  bool mapped_arguments=false;
  [[nodiscard]] std::span<const CellCapture> Captured(ExprId,TreeCheckBudget&) const;
};
[[nodiscard]] SourceCells AnalyzeSourceCells(const FunctionTree&,TreeCheckBudget&);
[[nodiscard]] CellVisibility JoinCellVisibility(CellVisibility,CellVisibility);
}  // namespace abcd::source
#endif
