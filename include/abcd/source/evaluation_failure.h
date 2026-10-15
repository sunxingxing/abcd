#ifndef ABCD_SOURCE_EVALUATION_FAILURE_H_
#define ABCD_SOURCE_EVALUATION_FAILURE_H_
#include "abcd/source/tree_failure.h"
#include "abcd/source/for_in_failure.h"
#include "abcd/source/concurrent_failure.h"
#include "abcd/source/evaluation_kind.h"
namespace abcd::source {
enum class EvaluationError : std::uint8_t {
  kRevision, kInvalidGraph, kUnsupportedOperation, kProtocolOperand, kLivePhi,
  kOwnership, kOrder, kUnavailableValue, kWorkLimit, kResourceLimit, kCatchState
};
// Diagnostic obligations, never admission predicates. All witnesses have fixed
// size and preserve absent locations; PCs are mapped only from input origins.
enum class EvaluationObligation : std::uint8_t {
  kPlan, kProducerOwnership, kInlineConsumer, kPhiInput, kLogicalInput,
  kRootTemporary, kDuplicateVisit, kOperandOwnership, kLiveCoverage,
  kPhaseRoot, kPhaseConsumption, kProjection, kTemporaryPresence, kTemporaryReason, kLogicalJoin, kLogicalInterval, kSnapshotCandidate, kLogicalCoverage, kUse
};
enum class SelectionExitReason : std::uint8_t {
  kNone, kNoUse, kMultipleUses, kPhiUse, kCrossBlock, kProtocolRole, kSnapshot, kKeepEdge
};
// Candidate diagnostics describe the proof input, not a new admission rule.
enum class SnapshotCandidateReason : std::uint8_t {
  kEligible, kNoOccurrence, kDeadUse, kPhiOwner, kCrossBlock, kAliasOwner,
  kInitializedGuard, kProducerPlacement, kProducerShape, kProducerExpression,
  kProducerInputs, kConsumerShape, kConsumerInputs, kSnapshotRole,
  kPredicateInput, kConsumerPlacement
};
struct SnapshotCandidateEvidence {
  SnapshotCandidateReason reason = SnapshotCandidateReason::kNoOccurrence;
  std::optional<analysis::UseId> occurrence;
  std::optional<EvaluationKind> producer, consumer;
  bool operator==(const SnapshotCandidateEvidence&) const = default;
};
enum class LogicalCoverageCheck : std::uint8_t {
  kValueMap, kRouteMember, kForwardedRegistration, kConsumedEvent
};
struct LogicalCoverageEvidence {
  LogicalCoverageCheck check = LogicalCoverageCheck::kValueMap;
  std::optional<analysis::OpId> branch, route_entry, route_exit;
  bool registered = false, covered = false;
  bool operator==(const LogicalCoverageEvidence&) const = default;
};
struct EvaluationEvidence {
  EvaluationObligation obligation = EvaluationObligation::kPlan;
  TreeLocation location;
  std::optional<ForInFailure> for_in;
  std::optional<ConcurrentFailure> concurrent;
  std::optional<SnapshotCandidateEvidence> snapshot_candidate;
  std::optional<LogicalCoverageEvidence> logical_coverage;
  std::optional<analysis::UseId> use, expected_consumer, actual_consumer;
  std::optional<analysis::ValueId> value;
  std::optional<SelectionExitReason> selection_exit;
  std::optional<analysis::EdgeId> selection_edge;
  std::optional<EvaluationKind> placement;
  std::optional<TemporaryReason> temporary, expected_temporary;
  std::optional<TreeLocation> expected_owner, actual_owner;
  bool operator==(const EvaluationEvidence&) const = default;
};
struct EvaluationFailure {
  EvaluationError kind;
  EvaluationEvidence evaluation;

  EvaluationFailure(EvaluationError error) : kind(error) {} // NOLINT: typed envelope.
  EvaluationFailure(EvaluationError error, EvaluationEvidence evidence) : kind(error), evaluation(evidence) {}
  bool operator==(EvaluationError error) const { return kind == error; }
};
} // namespace abcd::source
#endif
