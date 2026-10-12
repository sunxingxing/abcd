#ifndef ABCD_SOURCE_CAPTURE_EVALUATION_H_
#define ABCD_SOURCE_CAPTURE_EVALUATION_H_

#include "capture_cells.h"
#include "evaluation_plan.h"

namespace abcd::source {
struct CapturedFunctionBinding {
  FunctionBinding binding;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class CaptureEvaluationError : std::uint8_t {
  kOwner, kCreation, kOuterCapture, kResourceLimit, kWorkLimit
};
using CaptureEvaluationFailure = std::variant<CaptureCellFailure, CaptureEvaluationError>;
// Connects an actual input/work capture proof to the common evaluation table.
// Borrowed products must remain unchanged. Owners identify syntax instances,
// not methods: two creations of the same method require distinct child owners.
// This discharges cell correspondence only, not child bodies, names, callable
// metadata or transitive writes. An outward capture is never silently dropped.
[[nodiscard]] std::variant<CapturedFunctionBinding, CaptureEvaluationFailure>
BindCaptureEvaluation(const abc::MetadataIndex& index, CaptureFunction parent,
    CaptureChild child, analysis::InstructionId site,
    const analysis::CreationBinding& creation, const CaptureCells& cells,
    FunctionOwnerId parent_owner, FunctionOwnerId child_owner,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
