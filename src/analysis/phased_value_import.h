#ifndef ABCD_ANALYSIS_PHASED_VALUE_IMPORT_H_
#define ABCD_ANALYSIS_PHASED_VALUE_IMPORT_H_

#include "abcd/analysis/exception_inputs.h"
#include "abcd/analysis/value_import.h"

namespace abcd::analysis {
// All analyses must belong to the same unchanged BuildExceptionFlow input.
// Borrowed for this call only; source owners must separately certify creation,
// module/capture bindings, normalization, dispatch and source-contract premises.
struct PhasedValueInput {
  const ExceptionFlow& flow;
  const ExceptionSsa& ssa;
  const EnvironmentFlow& environments;
  const ExceptionValueFlow& values;
  const ExceptionInputProof& proof;
  const abc::FrameLayout& frame;
};
struct PhasedImportProof { std::uint64_t work; };
// Low-level import, not a source receipt. Additional owned graph and scratch
// share the budget; caller separately admits retained input analyses.
[[nodiscard]] std::variant<ValueProgram, ValueImportFailure> ImportPhasedValues(
    const PhasedValueInput& input, SemanticBudget budget = {});
// Independent comparison of the unedited graph, not another importer run.
[[nodiscard]] std::variant<PhasedImportProof, ValueImportFailure> VerifyImportedPhases(
    const ValueProgram& graph, const PhasedValueInput& input, SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
