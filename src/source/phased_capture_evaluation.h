#ifndef ABCD_SOURCE_PHASED_CAPTURE_EVALUATION_H_
#define ABCD_SOURCE_PHASED_CAPTURE_EVALUATION_H_

#include "capture_evaluation.h"
#include "phased_capture_cells.h"

namespace abcd::source {
// Same conditional contract as normal capture evaluation. Owners identify
// syntax occurrences, not method templates; complete child trees and writes
// remain mandatory before any parent/source publication.
[[nodiscard]] std::variant<CapturedFunctionBinding, CaptureEvaluationFailure> BindPhasedCaptureEvaluation(
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const PhasedBody& parent, const abc::MethodBody& body,
    const analysis::ExceptionLocals& locals, const PhasedCreationSite& creation,
    CaptureFunction child, const FunctionContext& owner, const CaptureCells& cells,
    FunctionOwnerId parent_owner, FunctionOwnerId child_owner, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
