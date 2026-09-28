#ifndef ABCD_SOURCE_PHASED_CAPTURE_INPUT_H_
#define ABCD_SOURCE_PHASED_CAPTURE_INPUT_H_

#include "function_input.h"
#include "phased_creations.h"

namespace abcd::source {
// Same unchanged file/body and freshly computed ExceptionLocals as parent.
// Parent input validation must discharge EVERY original store after applying
// local load premises. Conditional writes remain recorded in CapturePlan;
// working-cell rebasing, child ASTs and complete family writes are downstream.
[[nodiscard]] InputBindingResult BindPhasedFunctionInputs(
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const PhasedBody& parent, const abc::MethodBody& body,
    const analysis::ExceptionLocals& locals, const PhasedCreationSite& creation,
    const FunctionInput& child, const FunctionContext& owner, analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, CaptureError, InputBindingError> VerifyPhasedFunctionInputs(
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const PhasedBody& parent, const abc::MethodBody& body,
    const analysis::ExceptionLocals& locals, const PhasedCreationSite& creation,
    const FunctionInput& child, const FunctionContext& owner, const CapturePlan& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
