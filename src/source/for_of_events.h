#ifndef ABCD_SOURCE_FOR_OF_EVENTS_H_
#define ABCD_SOURCE_FOR_OF_EVENTS_H_
#include "for_of_iteration.h"
namespace abcd::source {
enum class ForOfEventKind : std::uint8_t {
  kAcquire, kNextRead, kNextCall, kObjectCheck, kDoneRead, kDoneDecision, kValueRead
};
struct ForOfEvent {
  IteratorProtocolId protocol;
  ForOfEventKind kind;
  analysis::OpId operation;
  analysis::WorkBlockId before, commit;
  analysis::ExceptionNodeId original_before, original_commit;
  analysis::OriginId origin;
  std::optional<analysis::ValueId> result;
  // Index into the original working graph, never a source expression.
  std::optional<std::uint32_t> exception_exit;
  bool operator==(const ForOfEvent&) const = default;
};
struct ForOfValueCommit {
  ForOfKeyDefinition definition;
  analysis::WorkBlockId commit;
  analysis::ExceptionNodeId original_commit;
  analysis::CompletionEdge success, exhausted;
  bool operator==(const ForOfValueCommit&) const = default;
};
struct ForOfClosePort {
  IteratorProtocolId protocol;
  ControlNodeId unit;
  analysis::CompletionEdge entry;
  analysis::PendingCompletion pending;
  analysis::CloseOutcomeLeaving outcome;
  bool operator==(const ForOfClosePort&) const = default;
};
// Standalone event correspondence, not native binding or evaluation admission.
// ValueCommit retains each physical definition; selecting the source binding
// still requires declaration/TDZ/live-out evidence. No invented ExprIds.
struct ForOfEvents {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ForOfEvent> events;
  std::vector<ForOfValueCommit> commits;
  std::vector<ForOfClosePort> ports;
  std::size_t allocated_bytes=0, peak_bytes=0;
  std::uint64_t work=0;
};
[[nodiscard]] std::variant<ForOfEvents,ForOfIterationFailure> BuildForOfEvents(
    const abc::MethodBody&, const analysis::ValueProgram&, const analysis::PhasedValueInput&,
    const ExceptionPlan&, const ExceptionControl&, const VerifiedProtocolRegionOwnership&,
    SourceContract, const ForOfIdentityIndex&, const ForOfIterationProof&, analysis::SemanticBudget = {});
[[nodiscard]] std::variant<std::uint64_t,ForOfIterationFailure> VerifyForOfEvents(
    const abc::MethodBody&, const analysis::ValueProgram&, const analysis::PhasedValueInput&,
    const ExceptionPlan&, const ExceptionControl&, const VerifiedProtocolRegionOwnership&,
    SourceContract, const ForOfIdentityIndex&, const ForOfIterationProof&, const ForOfEvents&,
    analysis::SemanticBudget = {});
}
#endif
