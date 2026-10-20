#ifndef ABCD_SOURCE_CLOSE_ENTRIES_H_
#define ABCD_SOURCE_CLOSE_ENTRIES_H_

#include "iterator_protocol_scan.h"
#include "abcd/analysis/completion_relation.h"

namespace abcd::source {
enum class CloseEntryError : std::uint8_t {
  kInput, kAcquisition, kLoop, kGuard, kContinuation, kPayload, kCatch,
  kExternalEntry, kCorrespondence, kResourceLimit, kWorkLimit
};
struct CloseEntryFailure {
  CloseEntryError kind;
  std::optional<IteratorProtocolId> protocol;
  std::optional<analysis::OpId> read;
  std::optional<analysis::ExceptionNodeId> node;
  std::optional<analysis::CompletionRelationFailure> relation;
};
struct DerivedCloseEntry {
  analysis::CompletionEntry entry;
  // The original return/jump, or the actual throwing operation. A pending
  // Throw retains both the selected Catch and its real exception source.
  analysis::ExceptionNodeId origin;
  std::optional<analysis::ExceptionNodeId> catcher;
  std::optional<analysis::CompletionEdge> transfer;
};
enum class CloseEntryPrecision : std::uint8_t { kConservativeSentinelValues };
struct CloseIterationBoundary {
  analysis::ExceptionNodeId advance;
  analysis::CompletionEdge exhausted;
  analysis::ExceptionNodeId exit;
  analysis::ExceptionNodeId success;
  // An always-abrupt first iteration has no executable backedge. Keep that
  // absence typed; an advance site must never be labelled a loop header.
  std::optional<analysis::ExceptionNodeId> loop_header;
  // Resolved Value key on THIS advance result, discovered by SSA identity.
  // Absence is a body-boundary proof gap, not acquisition admission.
  std::optional<analysis::ExceptionNodeId> value;
  bool operator==(const CloseIterationBoundary&) const = default;
};
struct DerivedClose {
  IteratorProtocolId protocol;
  analysis::OpId read;
  CloseIterationBoundary iteration;
  analysis::ExceptionNodeId predicate, state_write;
  // A normal outcome resumes another certified close before its terminal.
  // This is an original Before boundary, never a replacement completion.
  std::optional<analysis::ExceptionNodeId> continuation;
  std::vector<DerivedCloseEntry> entries;
  std::vector<analysis::CompletionExit> exits;
  analysis::CompletionRelation relation;
  CloseEntryPrecision precision = CloseEntryPrecision::kConservativeSentinelValues;
};
struct CloseEntries {
  std::vector<DerivedClose> closes;
  // Conservative simultaneous scratch bound, not RSS or allocator usage.
  // Candidate verification currently exposes work but not its scratch peak;
  // its exclusive grant is observed in full, never silently undercounted.
  std::size_t peak_bytes = 0;
  std::uint64_t work = 0;
};
// Standalone, synchronous and original-input only. There are NO caller-supplied
// arrivals or completions. Revalidates the candidate scan and immutable import,
// derives original entry edges from a state-guarded return-read occurrence and
// its bypass continuation, then runs the raw may relation. This is entry
// evidence, NOT IteratorClose priority, ownership, or source admission.
// Unknown guard/sentinel facts remain conservative; no exception is removed.
// Shared method indexes and sparse query states, with N log N work admission;
// every buffer/delegated grant is prepaid. Exhaustion refuses the whole query.
[[nodiscard]] std::variant<CloseEntries, CloseEntryFailure> DeriveCloseEntries(
    const abc::MethodBody&, abc::ProfileId, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&, const IteratorKeys&,
    const IteratorProtocolScan&, analysis::SemanticBudget = {});
// Rebuilds indexes and derivations from immutable inputs. Producer discovery
// uses forward dominator stacks; verification discovers guards backwards on
// dominance ancestors. Neither producer entries nor its relation are used to
// discover the expected boundary. Typed scalar/continuation semantics are
// shared; this is not a second implementation of the pinned interpreter.
[[nodiscard]] std::variant<std::uint64_t, CloseEntryFailure> VerifyCloseEntries(
    const abc::MethodBody&, abc::ProfileId, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&, const IteratorKeys&,
    const IteratorProtocolScan&, const CloseEntries&, analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
