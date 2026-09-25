#ifndef ABCD_SOURCE_CALL_RESULTS_H_
#define ABCD_SOURCE_CALL_RESULTS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Discard only unobserved accumulator commits, never the call or its effects.
void ElideCallResults(const AstMethod& method, SourceAst& ast,
                     std::span<const std::size_t> uses);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_CALL_RESULTS_H_
