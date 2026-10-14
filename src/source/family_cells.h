#ifndef ABCD_SOURCE_FAMILY_CELLS_H_
#define ABCD_SOURCE_FAMILY_CELLS_H_

#include "family_file.h"

namespace abcd::source {
struct FamilyCellCost { std::size_t allocated_bytes; std::uint64_t work; };
// An occurrence-local external family requirement. The caller must bind the
// actual class member against the resulting creator Storage before completion.
// This summary is not an instruction access or an initialization certificate.
struct FamilyCellDemand { FamilyId creator; CellDemand demand; };
// Reverse occurrence preorder propagates storage requirements once per edge.
// Re-run after residual projection, before rebinding any capture/evaluation.
[[nodiscard]] std::variant<FamilyCellCost, CaptureCellFailure> PlanFamilyCells(
    FileFamily& family, analysis::SemanticBudget budget = {},
    std::span<const FamilyCellDemand> external = {});
}  // namespace abcd::source
#endif
