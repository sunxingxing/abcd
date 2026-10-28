#ifndef ABCD_SOURCE_EXCEPTION_CONTROL_H_
#define ABCD_SOURCE_EXCEPTION_CONTROL_H_

#include "control_plan.h"
#include "exception_plan.h"
#include "protocol_region_ownership.h"

namespace abcd::source {
struct PhaseControlUnit {
  analysis::WorkBlockId block;
  bool operator==(const PhaseControlUnit&) const = default;
};
struct TryControlUnit {
  analysis::TryId region;
  bool operator==(const TryControlUnit&) const = default;
};
using TryExitSource = std::variant<analysis::EdgeId, analysis::TryId>;
struct TryExitUnit {
  analysis::TryId region;
  TryExitSource source;
  analysis::WorkBlockId target;
  bool operator==(const TryExitUnit&) const = default;
};
// A close unit is entry-qualified. Its outcome ports keep the original edge,
// exception identity and correlated completion, not fabricated source values.
struct ProtocolCloseUnit {
  IteratorProtocolId protocol;
  analysis::CompletionEdge entry;
  analysis::PendingCompletion pending;
  std::uint32_t first_port = 0, port_count = 0;
  bool operator==(const ProtocolCloseUnit&) const = default;
};
using ExceptionControlUnit = std::variant<PhaseControlUnit, TryControlUnit, TryExitUnit, ProtocolCloseUnit>;
struct ControlArc {
  // Absence means a wrapper continuation, not an original operation/PC.
  std::optional<analysis::EdgeId> edge;
  bool operator==(const ControlArc&) const = default;
};
struct TryControlRegion {
  ControlId wrapper;
  ControlId body;
  ControlId handler;
  std::optional<analysis::WorkBlockId> exit;
  bool operator==(const TryControlRegion&) const = default;
};
struct IteratorContinuationProof;
struct ExceptionControl {
  std::shared_ptr<const IteratorContinuationProof> continuations;
  // The skeleton's original-node prefix addresses units, not ValueProgram
  // blocks; loop wrappers/transfers follow that prefix. Its original arcs map
  // through arcs. No ValueProgram is fabricated: phases/dispatch stay intact.
  ControlPlan skeleton;
  std::vector<ExceptionControlUnit> units;
  std::vector<ControlArc> arcs;
  // Original TryId indexed. Absence is checked against the inactive-region proof.
  std::vector<std::optional<TryControlRegion>> regions;
  // Disconnected phases certified unreachable by original ExceptionFlow.
  // Removed Before operations retain sites but no effects/reads/definitions.
  std::vector<analysis::WorkBlockId> inactive_phases;
  std::vector<ExceptionRegionRole> region_roles;
  std::vector<analysis::CloseOutcomeLeaving> protocol_ports;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ExceptionControlError : std::uint8_t {
  kInput, kRevision, kRegion, kCrossEntry, kMultipleExits, kCycle, kStructure,
  kWorkLimit, kResourceLimit
};
// Reuses common postdominator/region and natural-loop discovery. A single normal exit
// target per try is required after enclosing-loop transfers and effect-free
// exit corridors are projected; returns/throws need no synthetic normal exit. Jump corridors keep
// original blocks, edge copies and PC anchors; semantic catch scopes never move.
// Reducible projected loops use the same wrappers/transfers as normal control;
// irreducible regions and more general completion transfers remain typed refusals.
// Not source proof.
struct ExceptionControlFailure {
  ExceptionControlError kind;
  ControlFailure cause;
};
struct ExceptionControlRegionFailure {
  ExceptionControlError kind;
  analysis::TryId region;
  std::array<ExceptionExitEvidence,2> exits;
  std::optional<ExceptionRegionEvidence> exception_region;
};
[[nodiscard]] std::variant<ExceptionControl, ExceptionControlError, ExceptionControlFailure, ExceptionControlRegionFailure> BuildExceptionControl(
    const abc::MethodBody& body, const analysis::ValueProgram& graph,
    const analysis::PhasedValueInput& input, const ExceptionPlan& regions,
    analysis::SemanticBudget budget = {},
    const VerifiedProtocolRegionOwnership* protocols = nullptr,
    std::shared_ptr<const IteratorContinuationProof> continuations = {});
[[nodiscard]] std::variant<std::uint64_t, ExceptionControlError> VerifyExceptionControl(
    const abc::MethodBody& body, const analysis::ValueProgram& graph,
    const analysis::PhasedValueInput& input, const ExceptionPlan& regions,
    const ExceptionControl& control, analysis::SemanticBudget budget = {},
    const VerifiedProtocolRegionOwnership* protocols = nullptr);
}  // namespace abcd::source
#endif
