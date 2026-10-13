#ifndef ABCD_SOURCE_EXCEPTION_CONTROL_H_
#define ABCD_SOURCE_EXCEPTION_CONTROL_H_

#include "control_plan.h"
#include "exception_plan.h"

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
using ExceptionControlUnit = std::variant<PhaseControlUnit, TryControlUnit, TryExitUnit>;
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
struct ExceptionControl {
  // The skeleton's original-node prefix addresses units, not ValueProgram
  // blocks; loop wrappers/transfers follow that prefix. Its original arcs map
  // through arcs. No ValueProgram is fabricated: phases/dispatch stay intact.
  ControlPlan skeleton;
  std::vector<ExceptionControlUnit> units;
  std::vector<ControlArc> arcs;
  std::vector<TryControlRegion> regions;
  // Disconnected phases certified unreachable by original ExceptionFlow.
  // Removed Before operations retain sites but no effects/reads/definitions.
  std::vector<analysis::WorkBlockId> inactive_phases;
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
[[nodiscard]] std::variant<ExceptionControl, ExceptionControlError> BuildExceptionControl(
    const abc::MethodBody& body, const analysis::ValueProgram& graph,
    const analysis::PhasedValueInput& input, const ExceptionPlan& regions,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, ExceptionControlError> VerifyExceptionControl(
    const abc::MethodBody& body, const analysis::ValueProgram& graph,
    const analysis::PhasedValueInput& input, const ExceptionPlan& regions,
    const ExceptionControl& control, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
