#ifndef ABCD_SOURCE_CAPTURE_CELLS_H_
#define ABCD_SOURCE_CAPTURE_CELLS_H_

#include "function_values.h"
#include "capture_input.h"

namespace abcd::source {
struct FileFunction;
class ModuleCapturePrefix;
class DefaultBody;
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
// Family-owned descendant demand. Original accesses remain the prefix of uses;
// the suffix names incoming storage forwarded without a local load/store.
struct CaptureStorage {
  CellPlan cells;
  std::vector<CaptureUse> uses;
};
// Borrowed same-function products of FunctionInput::Build/BuildFunctionValues.
// Graph, environments and cells stay unchanged while binding; edits require
// renewed correspondence, environment/cell plans and a fresh binding. All
// underlying storage is admitted by the caller. This is not an AST certificate.
struct CaptureFunction {
  const FunctionInput& input;
  const FunctionValues& values;
  const CaptureStorage* storage = nullptr;
  const ModuleCapturePrefix* module_prefix = nullptr;
  const DefaultBody* defaults = nullptr;
};
class CaptureChild {
 public:
  CaptureChild(CaptureFunction function) : function_(function), storage_(function.storage) {}
  CaptureChild(const FunctionInput& input, const FunctionValues& values) : function_(CaptureFunction{input, values}) {}
  CaptureChild(const FilePhasedFunction& function, const CaptureStorage* storage = nullptr)
      : function_(&function), storage_(storage) {}
  CaptureInput Input() const;
  CaptureInput OriginalInput() const;
  const analysis::ValueProgram& Graph() const;
  const analysis::ValueEnvironments* Environments() const;
  const CellPlan* Cells() const;
  bool MatchesInput() const;
  const DefaultBody* Defaults() const;
 private:
  std::variant<CaptureFunction, const FilePhasedFunction*> function_;
  const CaptureStorage* storage_ = nullptr;
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
// Resolve the actual original class-root creation point, then reuse the common
// normal-parent/normal-or-phase-child cell binder. Defaults must freshly rebind
// this proof after residual graphs and descendant demands settle.
[[nodiscard]] std::variant<CaptureCells, CaptureCellError> BindClassRootCaptureCells(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const FileFunction& creator, CaptureChild child,
    const FunctionContext& owner, analysis::SemanticBudget = {});
[[nodiscard]] std::variant<std::uint64_t, CaptureCellError> VerifyClassRootCaptureCells(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const FileFunction& creator, CaptureChild child,
    const FunctionContext& owner, const CaptureCells&, analysis::SemanticBudget = {});
// One genuine same-file resolved creation. Per-function plans are prepared
// once, not rebuilt per creation. Same target at two sites produces two plans.
[[nodiscard]] std::variant<CaptureCells, CaptureCellError> BindCaptureCells(
    const abc::MetadataIndex& index, CaptureFunction parent, CaptureChild child,
    analysis::InstructionId site, const CaptureSite& creation,
    analysis::SemanticBudget budget = {});
// Checks the supplied cell map against WORKING creation topology and ORIGINAL
// initialized capture occurrences. Does not rebuild BindCaptureCells.
[[nodiscard]] std::variant<std::uint64_t, CaptureCellError> VerifyCaptureCells(
    const abc::MetadataIndex& index, CaptureFunction parent, CaptureChild child,
    analysis::InstructionId site, const CaptureSite& creation,
    const CaptureCells& plan, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
