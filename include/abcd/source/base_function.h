#ifndef ABCD_SOURCE_BASE_FUNCTION_H_
#define ABCD_SOURCE_BASE_FUNCTION_H_

#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_attributes.h"
#include "abcd/analysis/function_creation.h"
#include "abcd/source/local_lexical_body.h"

namespace abcd::source {
enum class FunctionEmitError {
  kCreation,
  kKind,
  kSendable,
  kFrame,
  kLength,
  kName,
  kResourceLimit
};
struct BaseFunction {
  analysis::FunctionCreation creation;
  abc::MethodAttributes attributes;
  abc::FrameLayout frame;
  std::string_view name;
};
using FunctionEmitResult =
    std::variant<std::size_t, FunctionEmitError, EmitError, BodyEmitFailure,
                 analysis::FlowFailure, analysis::EnvironmentFlowFailure,
                 analysis::LinearLexicalFailure>;

// Caller proves matching creation, target, decoded body, ABI and runtime-parsed
// name under the pinned 7.0 semantics. Emits a strict named function
// expression, not a record. Only non-sendable BASE_CONSTRUCTOR DEFINEFUNC,
// simple formals with matching length, and the local lexical linear body subset
// are supported. Inputs remain unchanged; preflight rejects semantic/configured
// budget errors before output. IO, backing-file changes, or actual allocation
// failure during repeated body preparation can leave a prefix: use atomic file
// publication. Scratch shares budget.bytes with body analysis; caller separately
// admits inputs, transient strings and output.
[[nodiscard]] FunctionEmitResult WriteBaseFunction(
    const BaseFunction& function, const abc::MethodBody& body,
    std::ostream& output, LocalLexicalBudget budget = {},
    const MethodConstants* constants = nullptr);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_BASE_FUNCTION_H_
