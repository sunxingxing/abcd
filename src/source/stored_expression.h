#ifndef ABCD_SOURCE_STORED_EXPRESSION_H_
#define ABCD_SOURCE_STORED_EXPRESSION_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Redirect a single-use acc expression into its adjacent register/local-cell
// destination. The original use counts must include exceptional phi inputs.
// Named member stores also absorb adjacent pure scalar values without moving
// their effectful Set or inferring own-property/class-field semantics.
// Run before identity-based reload elision: that pass can remove a later acc
// definition while relying on this original definition still being emitted.
void FoldStoredExpressions(const AstMethod& method, SourceAst& ast,
                           std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
