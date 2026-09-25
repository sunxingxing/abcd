#ifndef ABCD_SOURCE_UNUSED_RESULTS_H_
#define ABCD_SOURCE_UNUSED_RESULTS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Discard only unobserved accumulator commits, never RHS evaluation/effects.
void ElideUnusedResults(const AstMethod& method, SourceAst& ast,
                     std::span<const std::size_t> uses);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_UNUSED_RESULTS_H_
