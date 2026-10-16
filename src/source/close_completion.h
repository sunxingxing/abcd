#ifndef ABCD_SOURCE_CLOSE_COMPLETION_H_
#define ABCD_SOURCE_CLOSE_COMPLETION_H_
#include "abcd/source/source_contract.h"

#include "close_entries.h"
#include "abcd/analysis/close_outcome_relation.h"

namespace abcd::source {
// These are separate obligations: a priority fact never grants region ownership.
enum class CloseObligation : std::uint8_t {
  kGuard, kReference, kPriority, kOwnership, kNested, kCount
};
enum class CloseCompletionError : std::uint8_t {
  kInput, kCloseGuard, kReference, kClosePriority, kCompletion,
  kExceptionCorrespondence, kOwnership, kExternalEntry, kNested,
  kCorrespondence, kResourceLimit, kWorkLimit
};
enum class ClosePriorityReason : std::uint8_t {
  kOutcomeUnproved, kSentinelPath, kExpectedOriginal, kExpectedCloseError,
  kExpectedTypeError, kMissingExit
};
// Priority-table transfer only. The outcome derivation must be verified before
// this function can contribute to a certificate; it grants no provenance.
[[nodiscard]] std::optional<ClosePriorityReason> ClosePriorityMismatch(
    const analysis::PendingCompletion&, const analysis::CloseOutcomeLeaving&);
enum class CloseOwnershipGap : std::uint8_t { kEventCoverage, kPrivateState, kRegionCoverage, kLiveOut, kUserTry, kPort, kRawFlow };
struct CloseCompletionFailure {
  CloseCompletionError kind;
  std::optional<IteratorProtocolId> protocol;
  std::optional<analysis::OpId> read;
  std::optional<std::uint32_t> entry;
  std::optional<analysis::ExceptionNodeId> node;
  std::optional<analysis::CompletionEdge> edge;
  std::optional<CloseEntryFailure> derivation;
  std::optional<ClosePriorityReason> priority;
  std::optional<analysis::CloseOutcomeGap> outcome_gap;
  std::optional<CloseOwnershipGap> ownership_gap;
  std::optional<analysis::TryId> region;
  bool operator==(const CloseCompletionFailure& b) const {
    // Derivation failures are returned as whole-query failures, never embedded
    // in a successful summary. Do not invent equality for opaque subproofs.
    return kind==b.kind && protocol==b.protocol && read==b.read && entry==b.entry &&
        node==b.node && edge==b.edge && priority==b.priority && outcome_gap==b.outcome_gap && ownership_gap==b.ownership_gap && region==b.region && !derivation && !b.derivation;
  }
};
enum class CloseProofState : std::uint8_t { kMissing, kVerified };
struct CloseObligationResult {
  CloseProofState state = CloseProofState::kMissing;
  std::optional<CloseCompletionFailure> missing;
  bool operator==(const CloseObligationResult&) const = default;
};
// Raw member roles account for state and control even when an obligation is
// missing. No mask from this diagnostic result authorizes elimination.
enum class CloseMemberRole : std::uint8_t {
  kScalar, kCatch, kReturnRead, kReturnCall, kResultCheck, kCompletion, kUnproved,
  kComparison, kInfeasible
};
struct CloseMember {
  analysis::ExceptionNodeId node;
  CloseMemberRole role;
  bool operator==(const CloseMember&) const = default;
};
struct CloseCompletionEntry {
  IteratorProtocolId protocol;
  analysis::OpId read;
  analysis::CompletionEdge source;
  analysis::PendingCompletion pending;
  std::array<CloseObligationResult, static_cast<std::size_t>(CloseObligation::kCount)> obligations;
  bool operator==(const CloseCompletionEntry&) const = default;
};
// A port is an original relation exit with its entry-qualified completion and
// outcome. No join PC or source ExprId substitutes for the physical edge.
struct ClosePrivateSlot {
  std::optional<abc::RegisterId> reg; // absent = acc, not an absent slot
  auto operator<=>(const ClosePrivateSlot&) const = default;
};
struct CloseBoundaryProof {
  CloseProofState entry_coverage = CloseProofState::kMissing;
  CloseProofState port_coverage = CloseProofState::kMissing;
  CloseProofState private_state = CloseProofState::kMissing;
  CloseProofState region_coverage = CloseProofState::kMissing;
  std::vector<analysis::CloseOutcomeLeaving> ports;
  std::vector<analysis::CompletionEdge> unreachable_entries;
  std::vector<ClosePrivateSlot> private_slots;
  std::vector<analysis::TryId> regions;
  std::optional<CloseCompletionFailure> missing;
  std::size_t peak_bytes = 0;
  std::uint64_t work = 0;
};
struct CloseCompletionRegion {
  IteratorProtocolId protocol;
  analysis::OpId read;
  std::optional<analysis::OpId> call;
  std::vector<CloseMember> members; // Original node order.
  std::vector<analysis::CompletionEdge> edges; // Every original member edge, cuts included.
  std::optional<analysis::CloseOutcomeRelation> outcomes;
  std::optional<CloseBoundaryProof> boundary;
};
struct CloseCompletionSummary {
  abc::EntityOffset method;
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<CloseCompletionEntry> entries;
  std::vector<CloseCompletionRegion> regions;
  std::size_t peak_bytes = 0;
  std::uint64_t work = 0;
};
// Standalone synchronous diagnostic analysis. Derives/validates its own entries;
// not caller-supplied assumptions. A result can contain missing obligations;
// it is NOT VerifiedProtocolRegionOwnership and has no production consumer.
// Conservative sentinel paths are never erased. Sparse walks are globally
// bounded by an input-sized N log N work grant, and all storage is prepaid.
[[nodiscard]] std::variant<CloseCompletionSummary, CloseCompletionFailure> AnalyzeCloseCompletion(
    const abc::MethodBody&, abc::ProfileId, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&, const IteratorKeys&,
    const IteratorProtocolScan&, analysis::SemanticBudget = {}, SourceContract = kSourceContract);
// Revalidates entries using A3a's backwards proof, recomputes raw membership,
// references and the exit relation from immutable inputs. Supplied flags,
// members, omissions and telemetry are never inputs to discovery.
[[nodiscard]] std::variant<std::uint64_t, CloseCompletionFailure> VerifyCloseCompletion(
    const abc::MethodBody&, abc::ProfileId, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&, const IteratorKeys&,
    const IteratorProtocolScan&, const CloseCompletionSummary&, analysis::SemanticBudget = {}, SourceContract = kSourceContract);
}  // namespace abcd::source
#endif
