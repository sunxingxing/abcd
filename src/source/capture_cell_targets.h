#ifndef ABCD_SOURCE_CAPTURE_CELL_TARGETS_H_
#define ABCD_SOURCE_CAPTURE_CELL_TARGETS_H_

#include "capture_cells.h"
#include "class_initializers.h"

namespace abcd::source {
// Allocation-free view assembled only after the normal/phased owner boundary
// has checked the same-file edge. Original and working environment IDs remain
// separate namespaces; allocations below are ORIGINAL instruction provenance.
struct CaptureParent {
  const RouteInputIdentity& identity;
  abc::EntityOffset method;
  const analysis::ValueProgram& graph;
  const analysis::ValueEnvironments* environments;
  const CellPlan* cells;
  std::span<const analysis::SemanticInstruction> instructions;
  std::span<const std::optional<analysis::InstructionId>> allocations;
  ScopeFacts scopes;
  // Normal creators: certified stored instance initializers. Their slot is
  // consumed by class syntax; no child may capture it (capture_cells.cc).
  std::span<const ClassInstanceInitializer> instances = {};
};
[[nodiscard]] std::variant<CaptureCells, CaptureCellFailure> BindCaptureTargets(
    CaptureParent parent, CaptureChild child, analysis::InstructionId site,
    const CaptureSite& creation, CapturePlan input,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, CaptureCellFailure> VerifyCaptureTargets(
    CaptureParent parent, CaptureChild child, analysis::InstructionId site,
    const CaptureSite& creation, const CaptureCells& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
