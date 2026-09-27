#ifndef ABCD_SOURCE_PHASED_CAPTURE_CELLS_H_
#define ABCD_SOURCE_PHASED_CAPTURE_CELLS_H_

#include "capture_cells.h"
#include "phased_capture_input.h"

namespace abcd::source {
// Fresh unchanged parent/child products, same original-input obligations as
// BindPhasedFunctionInputs. Reuses normal capture targets and their independent
// occurrence verifier; this does not close child ASTs or transitive writes.
[[nodiscard]] std::variant<CaptureCells, CaptureCellError> BindPhasedCaptureCells(
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const PhasedBody& parent, const abc::MethodBody& body,
    const analysis::ExceptionLocals& locals, const PhasedCreation& creation,
    CaptureFunction child, const FunctionContext& owner, analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, CaptureCellError> VerifyPhasedCaptureCells(
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const PhasedBody& parent, const abc::MethodBody& body,
    const analysis::ExceptionLocals& locals, const PhasedCreation& creation,
    CaptureFunction child, const FunctionContext& owner, const CaptureCells& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
