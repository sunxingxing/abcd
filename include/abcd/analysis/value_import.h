#ifndef ABCD_ANALYSIS_VALUE_IMPORT_H_
#define ABCD_ANALYSIS_VALUE_IMPORT_H_

#include "abcd/analysis/semantic_validation.h"
#include "abcd/analysis/value_ir.h"

namespace abcd::analysis {
enum class ValueImportError : std::uint8_t {
  kUnsupportedOperation, kExceptionFlow, kResumeFlow, kInputMismatch,
  kOperandMismatch, kInvalidPhi, kInvalidGraph, kWorkLimit, kResourceLimit
};
struct ValueImportFailure {
  ValueImportError kind;
  std::optional<abc::CodeOffset> pc;
};
// Low-level import primitive, not a publishable method receipt. Caller retains
// an unchanged, same-body/profile/frame SemanticMethod and SemanticProof.
// Budget covers additional owned graph + scratch; all input storage must be
// admitted by the caller alongside this allowance. No strings/legacy AST.
// Input identity/proof envelopes are mandatory at the source-route boundary.
[[nodiscard]] std::variant<ValueProgram, ValueImportFailure> ImportNormalValues(
    const abc::MethodBody& body, const SemanticMethod& method,
    const SemanticProof& proof, const abc::FrameLayout& frame,
    SemanticBudget budget = {});
// Independent traversal of the unedited imported graph against its immutable
// input. Does not reconstruct another graph or certify transformed revisions.
[[nodiscard]] std::optional<ValueImportFailure> VerifyImportedValues(
    const ValueProgram& graph, const SemanticMethod& method,
    const SemanticProof& proof, SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
