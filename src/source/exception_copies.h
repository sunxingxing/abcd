#ifndef ABCD_SOURCE_EXCEPTION_COPIES_H_
#define ABCD_SOURCE_EXCEPTION_COPIES_H_

#include "phi_copies.h"

namespace abcd::source {
struct CatchSlotId { std::uint32_t value; auto operator<=>(const CatchSlotId&) const = default; };
struct CatchSlot {
  analysis::WorkBlockId block;
  analysis::ValueId destination;
};
struct EntryCopies { bool operator==(const EntryCopies&) const = default; };
struct NormalCopies {
  analysis::EdgeId edge;
  bool operator==(const NormalCopies&) const = default;
};
struct ThrowCopies {
  analysis::EdgeId edge;
  analysis::OpId operation;
  analysis::WorkBlockId handler;
  bool operator==(const ThrowCopies&) const = default;
};
using CopyPlacement = std::variant<EntryCopies, NormalCopies, ThrowCopies>;
struct CatchCommit {
  analysis::WorkBlockId block;
  CatchSlotId first;
  std::uint32_t count;
};
enum class ExceptionCopyProjection : std::uint8_t { kOriginal, kForOfClose };
struct ExceptionCopies {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  ExceptionCopyProjection projection = ExceptionCopyProjection::kOriginal;
  std::vector<CopyPlacement> placements;  // PhiBundle indexed.
  // ParallelCopy indexed. Only exceptional bundles have staging destinations.
  std::vector<std::optional<CatchSlotId>> staging;
  std::vector<CatchSlot> slots;
  std::vector<CatchCommit> commits;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ExceptionCopyError : std::uint8_t {
  kRevision, kPhi, kPlacement, kSlot, kCommit, kResourceLimit, kWorkLimit
};
// Before a throwing operation, copy only to separate, invocation-private slots.
// On normal completion no phi location changes. At Catch entry, commit the slots
// to phi locations. Never emit the exceptional bundle's abstract EdgeCopies
// directly before the operation: its destinations may still hold live values.
// Slot storage must not alias parameters, cells, phi bindings or cycle temps.
// No CFG mutation, source binding or region/dispatch certification is implied.
// Beyond shared phi validation: O(V + P log P + U) work, O(V + P + U)
// additional storage for V values, P catch phis and U phi input occurrences.
// Input graph/copy storage is caller-admitted; results and scratch share budget.
[[nodiscard]] std::variant<ExceptionCopies, ExceptionCopyError> BuildExceptionCopies(
    const analysis::ValueProgram& graph, const PhiCopies& copies,
    analysis::SemanticBudget budget = {}, std::span<const ForOfUseDisposition> for_of = {},
    ExceptionCopyProjection projection = ExceptionCopyProjection::kOriginal);
[[nodiscard]] std::variant<std::uint64_t, ExceptionCopyError> VerifyExceptionCopies(
    const analysis::ValueProgram& graph, const PhiCopies& copies,
    const ExceptionCopies& plan, analysis::SemanticBudget budget = {}, std::span<const ForOfUseDisposition> for_of = {},
    ExceptionCopyProjection projection = ExceptionCopyProjection::kOriginal);
}  // namespace abcd::source
#endif
