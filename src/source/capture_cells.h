#ifndef ABCD_SOURCE_CAPTURE_CELLS_H_
#define ABCD_SOURCE_CAPTURE_CELLS_H_

#include "function_values.h"

namespace abcd::source {
struct ParentCell {
  CellId cell;
  bool operator==(const ParentCell&) const = default;
};
struct OuterCell {
  analysis::CapturedSlot cell;
  bool operator==(const OuterCell& other) const {
    return cell.depth == other.cell.depth && cell.slot.value == other.cell.slot.value;
  }
};
using CaptureTarget = std::variant<ParentCell, OuterCell>;
// Borrowed same-function products of FunctionInput::Build/BuildFunctionValues.
// Graph, environments and cells stay unchanged while binding; edits require
// renewed correspondence, environment/cell plans and a fresh binding. All
// underlying storage is admitted by the caller. This is not an AST certificate.
struct CaptureFunction {
  const FunctionInput& input;
  const FunctionValues& values;
};
struct CaptureCellBinding {
  CellId child;
  CaptureTarget target;
};
struct CaptureCells {
  RouteInputIdentity identity;
  abc::EntityOffset parent;
  abc::EntityOffset child;
  analysis::IrRevision parent_revision;
  analysis::CfgRevision parent_cfg;
  analysis::IrRevision child_revision;
  analysis::CfgRevision child_cfg;
  analysis::OpId creation;
  CapturePlan input;
  // Sorted by child CellId, incoming cells only. Each incoming cell aliases
  // parent mutable storage or retains an outward binding obligation.
  // An OuterCell is never an implicit declaration or a loaded-value snapshot.
  std::vector<CaptureCellBinding> targets;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class CaptureCellError : std::uint8_t {
  kOwner, kRevision, kCreation, kInput, kCell, kCoverage, kBinding,
  kResourceLimit, kWorkLimit
};
// One genuine same-file resolved creation. Per-function plans are prepared
// once, not rebuilt per creation. Same target at two sites produces two plans.
[[nodiscard]] std::variant<CaptureCells, CaptureCellError> BindCaptureCells(
    const abc::MetadataIndex& index, CaptureFunction parent, CaptureFunction child,
    analysis::InstructionId site, const analysis::CreationBinding& creation,
    analysis::SemanticBudget budget = {});
// Checks the supplied cell map against WORKING creation topology and ORIGINAL
// initialized capture occurrences. Does not rebuild BindCaptureCells.
[[nodiscard]] std::variant<std::uint64_t, CaptureCellError> VerifyCaptureCells(
    const abc::MetadataIndex& index, CaptureFunction parent, CaptureFunction child,
    analysis::InstructionId site, const analysis::CreationBinding& creation,
    const CaptureCells& plan, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
