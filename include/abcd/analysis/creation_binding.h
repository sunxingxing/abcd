#ifndef ABCD_ANALYSIS_CREATION_BINDING_H_
#define ABCD_ANALYSIS_CREATION_BINDING_H_

#include "abcd/abc/method_target.h"
#include "abcd/analysis/environment_flow.h"
#include "abcd/analysis/function_creation.h"
#include "abcd/analysis/exception_phase.h"

namespace abcd::analysis {
struct ExceptionFlow;
struct OrdinaryCapture {
  EnvironmentRef environment;
};
// DEFINEFUNC's empty shared-function lexical env, not the caller's ordinary
// chain. The generated sendable module's separate environment is unresolved.
struct SendableCapture {};
using CreationCapture = std::variant<OrdinaryCapture, SendableCapture>;
struct CapturedSlot {
  CapturedDepth depth;
  LexicalSlot slot;
};
// A use obligation, not part of cell identity. Rebase the cell without
// weakening this requirement; initialization alone proves only Language.
enum class CaptureDomain : std::uint8_t { kLanguage, kDefined };
enum class CaptureRebaseError { kSendableEnvironment };

// One ordinary creation edge: input is a target-method captured binding;
// result belongs to the caller index. Local target bindings are not inputs.
// Preserve creation-site context with the result; neither shared target methods
// nor equal allocation-site IDs imply shared runtime cells. No TDZ/value proof.
[[nodiscard]] std::variant<LexicalBinding, EnvironmentError, CaptureRebaseError>
RebaseCapture(const EnvironmentIndex& caller, const CreationCapture& creation,
              CapturedSlot captured);

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
// Same target/capture rules, but environments are indexed by exception nodes.
// Only the original creation's Before node is valid; Commit/Catch cannot stand
// in for the capture point. Unchanged successful phase/environment analyses are
// caller prerequisites. No normal-flow projection or per-call node scan.
[[nodiscard]] CreationBindingResult BindExceptionCreation(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::MethodBody& body, const ExceptionFlow& flow,
    const EnvironmentFlow& environments, ExceptionNodeId before);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_CREATION_BINDING_H_
