#ifndef ABCD_ANALYSIS_CREATION_BINDING_H_
#define ABCD_ANALYSIS_CREATION_BINDING_H_

#include "abcd/abc/method_target.h"
#include "abcd/analysis/environment_flow.h"
#include "abcd/analysis/function_creation.h"
#include "abcd/analysis/function_creation_sites.h"
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
// A local unexposed-prefix proof may establish Language before any child can
// execute, rather than at creation; it never establishes Defined or constness.
// kGuarded is a Language read whose loaded value reaches nothing but the
// method's own Hole guard before that guard completes. Its uninitialized
// observation is the guard's ReferenceError, so a parent may discharge it with
// a native TDZ declaration at the initializing store instead of initialization.
// kSourceAccess: an unguarded captured load, or a captured store, outside
// every handler of its own method, recorded only under the approved source
// TDZ premise (language-source.md; reads approved 2026-10-13, extended to
// writes the same day). Its value is Language; the parent may defer the
// binding's declaration to its first store, as for kGuarded. Handler-covered
// accesses keep kLanguage unless an independent guarded-store/PutValue proof
// retains the original TDZ guard and exception destination.
enum class CaptureDomain : std::uint8_t { kLanguage, kDefined, kGuarded, kSourceAccess };
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
  // Object-literal key ordinal of a buffer method; kNoLiteralProperty otherwise.
  std::uint32_t property = kNoLiteralProperty;
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
// One indexed object-literal method occurrence at its unchanged modern
// CREATEOBJECTWITHBUFFER instruction. v7 CloneObjectLiteral installs the
// current ordinary lexical env on every cloned method (no sendable branch) and
// the fresh literal as home object. The site must name this exact instruction.
[[nodiscard]] CreationBindingResult BindLiteralCreation(
    const abc::MethodBody& body, const EnvironmentFlow& flow, std::size_t instruction,
    const FunctionCreationSite& site);
// Exception-node form; only the creation's own Before node is valid.
[[nodiscard]] CreationBindingResult BindExceptionLiteralCreation(
    const abc::MethodBody& body, const ExceptionFlow& flow,
    const EnvironmentFlow& environments, ExceptionNodeId before,
    const FunctionCreationSite& site);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_CREATION_BINDING_H_
