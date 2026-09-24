#ifndef ABCD_SOURCE_STRUCTURED_CATCH_H_
#define ABCD_SOURCE_STRUCTURED_CATCH_H_

#include "abcd/source/base_function.h"

namespace abcd::source {
// A single catch-all with an explicit skip edge and isolated normal regions.
// Emits native try/catch in the containing function; unsupported exceptional
// joins remain refusals rather than silently losing the handler.
[[nodiscard]] FunctionEmitResult WriteStructuredCatch(
    const abc::MethodBody& body, ScalarNames names, std::ostream& output,
    LocalLexicalBudget budget, const MethodConstants* constants);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_STRUCTURED_CATCH_H_
