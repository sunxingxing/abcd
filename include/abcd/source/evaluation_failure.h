#ifndef ABCD_SOURCE_EVALUATION_FAILURE_H_
#define ABCD_SOURCE_EVALUATION_FAILURE_H_
#include "abcd/source/tree_failure.h"
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
struct EvaluationEvidence {
  EvaluationObligation obligation = EvaluationObligation::kPlan;
  TreeLocation location;
  std::optional<analysis::UseId> use, expected_consumer, actual_consumer;
  std::optional<analysis::ValueId> value;
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
