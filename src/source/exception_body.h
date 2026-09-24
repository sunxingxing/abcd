#ifndef ABCD_SOURCE_EXCEPTION_BODY_H_
#define ABCD_SOURCE_EXCEPTION_BODY_H_
#include "abcd/source/semantic_body.h"

namespace abcd::source {
// Raw same-body exception owner. No implicit host/capture premises or async
// normalization. A successful result has passed the shared AST and printer.
SemanticBodyResult BuildExceptionBody(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    ScalarNames names, const MethodConstants& constants,
    analysis::SemanticBudget budget, std::size_t output_bytes);
}  // namespace abcd::source
#endif
