#ifndef ABCD_ANALYSIS_CLOSE_OUTCOME_RELATION_H_
#define ABCD_ANALYSIS_CLOSE_OUTCOME_RELATION_H_

#include "abcd/analysis/completion_relation.h"

namespace abcd::analysis {
enum class CloseOutcomeKind : std::uint8_t {
  kUnproved, kSuccessObject, kMissingOrNullish, kGetterThrow, kCallThrow, kPrimitiveResult
};
enum class CloseOutcomeGap : std::uint8_t {
  kNone, kNotObserved, kOtherException, kRepeatedEvent, kUnprovedResult, kSentinelPath
};
struct CloseOutcome {
  CloseOutcomeKind kind = CloseOutcomeKind::kUnproved;
  std::optional<ExceptionNodeId> event;
  std::optional<CompletionPayload> exception;
  CloseOutcomeGap gap = CloseOutcomeGap::kNone;
  std::optional<ExceptionNodeId> uncertain_at;
  auto operator<=>(const CloseOutcome&) const = default;
};
struct CloseOutcomeQuery {
  ExceptionNodeId read, call; // Original Before identities, not source expressions.
  bool operator==(const CloseOutcomeQuery&) const = default;
};
struct CloseOutcomeLeaving {
  std::uint32_t entry;
  CompletionExit exit;
  PendingCompletion pending;
  CloseOutcome outcome;
  auto operator<=>(const CloseOutcomeLeaving&) const = default;
};
enum class ComparisonDomain : std::uint8_t { kUnknown, kUndefined, kNull, kHole, kFalse, kTrue, kInteger };
enum class ComparisonProofKind : std::uint8_t { kNullishOperand, kKnownOperands };
struct NonThrowingComparison {
  std::uint32_t entry;
  ExceptionNodeId node; // Original Before identity; facts are path-qualified.
  BinaryOperator operation;
  ComparisonDomain left, right;
  CompletionPayload left_value, right_value;
  ComparisonProofKind kind;
  std::optional<bool> equal; // Underlying equality, before the NotEqual inversion.
  auto operator<=>(const NonThrowingComparison&) const = default;
};
struct CloseOutcomeRelation {
  std::vector<CompletionArrival> arrivals;
  std::vector<CloseOutcomeLeaving> leaving;
  std::vector<NonThrowingComparison> comparisons;
  std::size_t peak_bytes = 0, states = 0;
  std::uint64_t work = 0;
};
// Original synchronous inputs and an observed read/call pair. This query
// observes outcomes; it grants no iterator identity, entries or region ownership.
// Nullish evidence follows the read's SSA value through copies and edge phis,
// loose equality with null/undefined and typed Boolean predicates. A check is
// a result check only on this call's successful result, on that same path.
// Outcome, entry, payload, target and SSA environment are ONE correlated state.
// A3d: per-path operand-domain comparison witnesses can discharge an exception
// edge and resolve concrete Hole equality. Unknown Catch payloads never acquire
// a non-Hole premise. The original graph and the A2 API are not changed.
// Unknown exceptions/sentinel paths stay unproved; exhaustion refuses the query.
[[nodiscard]] std::variant<CloseOutcomeRelation, CompletionRelationFailure> AnalyzeCloseOutcomeRelation(
    const abc::MethodBody&, abc::ProfileId, std::uint64_t frame_size,
    const ExceptionFlow&, const ExceptionSsa&, CloseOutcomeQuery,
    std::span<const CompletionEntry>, std::span<const CompletionExit>, CompletionRelationBudget = {});
// Immutable raw events and dispatch are rebuilt, observed roles are re-derived,
// and the relation is recomputed with the opposite worklist order. Producer
// outcomes, predicate evidence and supplied classification are never trusted.
[[nodiscard]] std::variant<CompletionRelationCost, CompletionRelationFailure> VerifyCloseOutcomeRelation(
    const abc::MethodBody&, abc::ProfileId, std::uint64_t frame_size,
    const ExceptionFlow&, const ExceptionSsa&, CloseOutcomeQuery,
    std::span<const CompletionEntry>, std::span<const CompletionExit>,
    const CloseOutcomeRelation&, CompletionRelationBudget = {});
}  // namespace abcd::analysis
#endif
