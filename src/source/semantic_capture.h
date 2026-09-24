#ifndef ABCD_SOURCE_SEMANTIC_CAPTURE_H_
#define ABCD_SOURCE_SEMANTIC_CAPTURE_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
// Rebase at the actual creation environment. Local cells require first capture
// after a same-block Language store; external cells remain typed obligations.
// The child template separately proves its captured stores remain Language.
std::variant<analysis::LocalCell, analysis::CapturedSlot, SourceFailure>
ResolveCreationCapture(
    const analysis::SemanticMethod& method,
    const analysis::SemanticProof& proof, const FunctionExpression& function,
    analysis::CapturedSlot capture,
    analysis::CaptureDomain requirement = analysis::CaptureDomain::kLanguage);
// Rebase through the exact creation Before environment. Outer bindings remain
// obligations; locals require the optional full-phase store/lifetime proof.
// The owner must validate all parent inputs before consuming that proof.
std::variant<analysis::LocalCell, analysis::CapturedSlot, SourceFailure>
ResolveCreationCapture(const analysis::EnvironmentFlow& environments,
    const FunctionExpression& function, analysis::CapturedSlot capture,
    abc::CodeOffset pc, const analysis::ExceptionLocals* locals = nullptr);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_SEMANTIC_CAPTURE_H_
