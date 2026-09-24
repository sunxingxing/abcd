#ifndef ABCD_SOURCE_SEMANTIC_CAPTURE_H_
#define ABCD_SOURCE_SEMANTIC_CAPTURE_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
// First capture since same-block allocation, after a Language store. The
// child template separately proves its captured stores remain Language.
std::variant<analysis::LocalCell, SourceFailure> ResolveLocalCapture(
    const analysis::SemanticMethod& method,
    const analysis::SemanticProof& proof, const FunctionExpression& function,
    analysis::CapturedSlot capture);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_SEMANTIC_CAPTURE_H_
