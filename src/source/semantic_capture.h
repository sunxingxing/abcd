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
// Phased topology only: rebases through the exact creation Before environment.
// Outer bindings remain parent obligations; topology cannot initialize locals.
std::variant<analysis::LocalCell, analysis::CapturedSlot, SourceFailure>
ResolveCreationCapture(const analysis::EnvironmentFlow& environments,
    const FunctionExpression& function, analysis::CapturedSlot capture,
    abc::CodeOffset pc);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_SEMANTIC_CAPTURE_H_
