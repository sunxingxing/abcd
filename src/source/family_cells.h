#ifndef ABCD_SOURCE_FAMILY_CELLS_H_
#define ABCD_SOURCE_FAMILY_CELLS_H_

#include "family_file.h"

namespace abcd::source {
struct FamilyCellCost { std::size_t allocated_bytes; std::uint64_t work; };
// Reverse occurrence preorder propagates storage requirements once per edge.
// Re-run after residual projection, before rebinding any capture/evaluation.
[[nodiscard]] std::variant<FamilyCellCost, CaptureCellFailure> PlanFamilyCells(
    FileFamily& family, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
