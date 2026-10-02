#ifndef ABCD_SOURCE_CELL_PLAN_H_
#define ABCD_SOURCE_CELL_PLAN_H_

#include "abcd/analysis/value_environment.h"
#include "abcd/analysis/creation_binding.h"

namespace abcd::source {
struct CellId { std::uint32_t value; auto operator<=>(const CellId&) const = default; };
struct AllocatedCell {
  analysis::OpId allocation;
  analysis::LexicalSlot slot;
};
struct IncomingCell {
  analysis::CapturedDepth depth;
  analysis::LexicalSlot slot;
};
using CellStorage = std::variant<AllocatedCell, IncomingCell>;
enum class CellAccessKind : std::uint8_t { kLoad, kStore };
struct CellAccess { analysis::OpId operation; CellAccessKind kind; };
struct Cell {
  CellStorage storage;
  std::uint32_t first;
  std::uint32_t count;
};
// A descendant's entry-slot requirement at a real function creation. This is
// storage topology only: the family must establish the child's demand and the
// creation's initialization/domain. It is not a fabricated lexical access.
struct CellDemand {
  analysis::OpId creation;
  analysis::CapturedSlot captured;
};
struct CellPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<Cell> cells;
  std::vector<CellAccess> accesses;
  std::vector<std::optional<CellId>> operations;
  // Working EnvironmentId -> allocation OpId, in this revision only.
  // Capture rebasing must never compare these IDs to an input owner's index.
  std::vector<analysis::OpId> allocations;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  std::vector<CellDemand> demands;
  // Incoming storage used outside the residual body (e.g. a native parameter
  // initializer). These are binding obligations, never fabricated body reads
  // or evidence of initialization. The signature owner must prove each demand.
  std::vector<IncomingCell> entry_demands;
};
// Query an unchanged verified plan. O(log(cells + 1)); no allocation.
[[nodiscard]] std::optional<CellId> FindCell(const CellPlan& plan, const CellStorage& storage);
enum class CellError : std::uint8_t {
  kRevision, kEnvironment, kStorage, kAccess, kCoverage, kResourceLimit, kWorkLimit
};
// Shared mutable storage, not scalar SSA. An allocation OpId names a dynamic
// allocation site: repeated activation/iteration still creates fresh cells.
// Incoming cells remain owner obligations. No initialized/const/hoist claim.
[[nodiscard]] std::variant<CellPlan, CellError> BuildCellPlan(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<CellPlan, CellError> BuildDemandedCellPlan(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    std::span<const CellDemand> demands, analysis::SemanticBudget budget = {},
    std::span<const IncomingCell> entry_demands = {});
[[nodiscard]] std::variant<std::uint64_t, CellError> VerifyCellPlan(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& plan, analysis::SemanticBudget budget = {});
// Exception topology only. Initialization/lifetime proofs must independently
// account for successful Commit and catch; normal certificates do not suffice.
[[nodiscard]] std::variant<CellPlan, CellError> BuildPhasedCellPlan(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<CellPlan, CellError> BuildDemandedPhasedCellPlan(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    std::span<const CellDemand> demands, analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, CellError> VerifyPhasedCellPlan(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& plan, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
