#ifndef ABCD_ANALYSIS_COMPLETION_RELATION_H_
#define ABCD_ANALYSIS_COMPLETION_RELATION_H_

#include "abcd/analysis/exception_ssa.h"
#include "abcd/analysis/raw_event_graph.h"
#include <compare>

namespace abcd::analysis {
enum class PendingCompletionKind : std::uint8_t { kNormal, kBreak, kContinue, kReturn, kThrow };
struct CompletionEntryPayload {
  std::uint32_t value;
  auto operator<=>(const CompletionEntryPayload&) const = default;
};
struct CompletionExceptionValue {
  ExceptionNodeId before;
  auto operator<=>(const CompletionExceptionValue&) const = default;
};
// monostate is an empty Normal/Break/Continue payload, or Return(undefined).
using CompletionPayload = std::variant<std::monostate, SsaValueId,
                                       CompletionEntryPayload, CompletionExceptionValue>;
struct CompletionCaller {
  auto operator<=>(const CompletionCaller&) const = default;
};
using CompletionTarget = std::variant<ExceptionNodeId, CompletionCaller>;
struct PendingCompletion {
  PendingCompletionKind kind = PendingCompletionKind::kNormal;
  CompletionPayload payload;
  CompletionTarget target = CompletionCaller{};
  auto operator<=>(const PendingCompletion&) const = default;
};
struct CompletionEdge {
  ExceptionNodeId source;
  std::uint8_t successor;
  auto operator<=>(const CompletionEdge&) const = default;
};
struct CompletionCallerExit {
  ExceptionNodeId source;
  auto operator<=>(const CompletionCallerExit&) const = default;
};
using CompletionExit = std::variant<CompletionEdge, CompletionCallerExit>;
struct CompletionEntry {
  CompletionEdge edge;
  PendingCompletion pending;
  // Optional source-edge-available SSA storage for a symbolic entry payload. This is an
  // explicit boundary assumption, NOT proof of the entry's completion kind.
  // Availability is re-proved on the original exception CFG; a destination
  // phi/new catch definition cannot be bound before crossing the entry edge.
  std::optional<SsaValueId> binding;
};
struct CompletionArrival {
  std::uint32_t entry;
  PendingCompletion pending;
  auto operator<=>(const CompletionArrival&) const = default;
};
struct CompletionLeaving {
  std::uint32_t entry;
  CompletionExit exit;
  PendingCompletion pending;
  auto operator<=>(const CompletionLeaving&) const = default;
};
struct CompletionRelation {
  std::vector<CompletionArrival> arrivals;
  std::vector<CompletionLeaving> leaving;
  std::size_t peak_bytes = 0, states = 0;
  std::uint64_t work = 0;
};
enum class CompletionRelationError : std::uint8_t {
  kInput, kEntry, kExit, kPayload, kPhi, kUnknownEffect, kCorrespondence,
  kResourceLimit, kWorkLimit
};
struct CompletionRelationFailure {
  CompletionRelationError kind;
  std::optional<std::uint32_t> entry;
  std::optional<ExceptionNodeId> node;
  std::optional<CompletionEdge> edge;
  std::optional<SsaValueId> value;
};
enum class CompletionReplay : std::uint8_t { kAwaits, kAllProtocols };
struct CompletionRelationBudget {
  SemanticBudget resources;
  std::size_t states = std::numeric_limits<std::size_t>::max();
  CompletionReplay replay = CompletionReplay::kAwaits;
};
struct CompletionRelationCost { std::size_t peak_bytes, states; std::uint64_t work; };
// Matching immutable synchronous body/ExceptionFlow/ExceptionSsa, built by the
// original analyzers. Entries are explicit boundary assumptions; exits are an
// edge cut. No entry classification, IteratorClose priority, region ownership,
// progress, dynamic-activation or source-elimination certificate is granted.
// SSA/exception payloads are static origin identities, not equality proofs
// between different dynamic executions of a loop definition.
// Normal edge cuts preserve the incoming completion context. An escaping
// exception replaces it with the actual payload and original handler/caller
// target; a return port replaces it with Return(value, Caller). Catching a close
// failure alone does NOT overwrite the pending context. Subsequent expanded
// code's SSA reads select the escaping value. The boundary assumptions must be
// proved separately by the future close-entry/ownership analysis.
// The may relation keeps entire tuples correlated. Scalar copies/constants and
// edge-selected SSA phis preserve identity. Unknown predicates retain both
// paths; every original possibly-throwing edge remains, including resource
// exceptions. Cycles need not terminate. Missing cuts/unknown raw effects and
// exhaustion refuse the WHOLE query, never return a partial or dead result.
// Sparse state admission is linear in input size; fixed-width persistent SSA
// maps and ordered interning give O((V+E) log V) admitted work/storage accounting.
[[nodiscard]] std::variant<CompletionRelation, CompletionRelationFailure> AnalyzeCompletionRelation(
    const abc::MethodBody&, abc::ProfileId, std::uint64_t frame_size,
    const ExceptionFlow&, const ExceptionSsa&, std::span<const CompletionEntry>,
    std::span<const CompletionExit>, CompletionRelationBudget = {});
// Rebuilds the raw graph and recomputes the relation with the opposite worklist
// order, from immutable inputs. No producer state, environment or coverage is
// trusted; telemetry is not a proof input. This verifies the relation under the
// supplied boundary assumptions, not those assumptions' semantic provenance.
[[nodiscard]] std::variant<CompletionRelationCost, CompletionRelationFailure> VerifyCompletionRelation(
    const abc::MethodBody&, abc::ProfileId, std::uint64_t frame_size,
    const ExceptionFlow&, const ExceptionSsa&, std::span<const CompletionEntry>,
    std::span<const CompletionExit>, const CompletionRelation&, CompletionRelationBudget = {});
}  // namespace abcd::analysis
#endif
