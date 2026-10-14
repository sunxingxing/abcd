#ifndef ABCD_SOURCE_ITERATOR_PROTOCOL_SCAN_H_
#define ABCD_SOURCE_ITERATOR_PROTOCOL_SCAN_H_

#include "iterator_keys.h"
#include "exception_plan.h"
#include <array>

namespace abcd::source {
struct IteratorProtocolId {
  std::uint32_t value = 0;
  auto operator<=>(const IteratorProtocolId&) const = default;
};
enum class IteratorObligation : std::uint8_t {
  kIdentity, kCachedNext, kClosedUse, kActivation, kAdvance, kDoneValueOrder,
  kBinding, kRegionOwnership, kCloseGuard, kOriginalException, kCloseException,
  kReturnResult, kCompletionPriority, kNestedClose, kExceptionCorrespondence,
  kCount
};
enum class IteratorEvidenceState : std::uint8_t { kMissing, kVerified };
struct IteratorOccurrence {
  analysis::OpId operation;
  analysis::ExceptionNodeId before;
  std::optional<abc::CodeOffset> pc;
  abc::CodeOffset working_pc;
  bool operator==(const IteratorOccurrence&) const = default;
};
struct IteratorProtocolCandidate {
  IteratorProtocolId id;
  IteratorOccurrence acquisition;
  analysis::ValueId iterator;
  std::optional<analysis::OpId> next;
  std::array<IteratorEvidenceState, static_cast<std::size_t>(IteratorObligation::kCount)> obligations{};
  bool operator==(const IteratorProtocolCandidate&) const = default;
};
// Association is deliberately separate from ownership: a selected dispatch
// identifies a real inner region, not permission to remove its handler.
enum class IteratorRegionRole : std::uint8_t { kReturnProtection, kEnclosingHandler };
struct IteratorRegionCandidate {
  IteratorProtocolId protocol;
  analysis::TryId region;
  IteratorRegionRole role;
  IteratorOccurrence read;
  std::optional<analysis::TryId> parent;
  bool operator==(const IteratorRegionCandidate&) const = default;
};
struct IteratorProtocolScan {
  abc::EntityOffset method;
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<IteratorProtocolCandidate> candidates;
  std::vector<IteratorRegionCandidate> regions;
  // Conservative simultaneous peak including delegated verifier grants.
  // Not a retained-size charge: only the two vector capacities survive.
  std::size_t allocated_bytes = 0;
  std::size_t retained_bytes = 0;
  std::uint64_t work = 0;
};
enum class IteratorScanError : std::uint8_t {
  kInput, kKeys, kIdentity, kCorrespondence, kResourceLimit, kWorkLimit
};
struct IteratorScanFailure {
  IteratorScanError kind;
  std::optional<analysis::OpId> operation;
  std::optional<analysis::UseId> use;
  bool operator==(const IteratorScanFailure&) const = default;
};
// Original input and unedited verified import only. Candidates are NOT region
// ownership. No topology/evaluation consumer may use a candidate as permission.
// O((V+U+E) log(V+U+1)); shared SCC identity analysis, no per-loop graph walks.
[[nodiscard]] std::variant<IteratorProtocolScan, IteratorScanFailure> ScanIteratorProtocols(
    const analysis::ValueProgram&, const analysis::PhasedValueInput&,
    const ExceptionPlan&, const IteratorKeys&, analysis::SemanticBudget = {});
// Independent field/occurrence checks against original phase/SSA/dispatch.
// This verifies candidate facts only, and cannot mint a region-ownership token.
[[nodiscard]] std::variant<std::uint64_t, IteratorScanFailure> VerifyIteratorCandidates(
    const analysis::ValueProgram&, const analysis::PhasedValueInput&,
    const ExceptionPlan&, const IteratorKeys&, const IteratorProtocolScan&,
    analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
