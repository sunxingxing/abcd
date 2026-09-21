#ifndef ABCD_ANALYSIS_CREATION_BINDING_H_
#define ABCD_ANALYSIS_CREATION_BINDING_H_

#include "abcd/abc/method_target.h"
#include "abcd/analysis/environment_flow.h"
#include "abcd/analysis/function_creation.h"

namespace abcd::analysis {
struct OrdinaryCapture {
  EnvironmentRef environment;
};
// DEFINEFUNC's empty shared-function lexical env, not the caller's ordinary
// chain. The generated sendable module's separate environment is unresolved.
struct SendableCapture {};
using CreationCapture = std::variant<OrdinaryCapture, SendableCapture>;
struct CreationBinding {
  abc::CodeOffset offset;
  FunctionCreation creation;
  abc::MethodTarget target;
  CreationCapture capture;
};
enum class CreationBindingError {
  kInstructionRange,
  kInvalidFlow,
  kUnreachable,
  kInvalidEnvironment
};
using CreationBindingResult =
    std::variant<CreationBinding, CreationError, CreationBindingError,
                 abc::MethodTargetError, abc::MetadataError,
                 abc::MethodAttributeError>;

// Bind one modern creation instruction in an unchanged validated caller body
// and its successful EnvironmentFlow. All file/index/body contexts must agree.
// Does not merge sites sharing a method, resolve captured values/home objects,
// infer source syntax, or certify historical runtime semantics. Returned local
// environment IDs belong to flow, not the target method's environment index.
// O(log R + log M), no retained allocation; the caller reuses a per-file
// lookup.
[[nodiscard]] CreationBindingResult BindCreation(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::MethodBody& body,
    const EnvironmentFlow& flow, std::size_t instruction);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_CREATION_BINDING_H_
