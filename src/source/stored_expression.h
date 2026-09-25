#ifndef ABCD_SOURCE_STORED_EXPRESSION_H_
#define ABCD_SOURCE_STORED_EXPRESSION_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Redirect a single-use acc expression into its adjacent register/local-cell
// destination. The original use counts must include exceptional phi inputs.
// Named stores also absorb adjacent scalar/string values without moving their
// write or weakening own-field provenance. Empty arrays require the explicit
// source contract excluding resource-failure ordering differences.
// Run before identity-based reload elision: that pass can remove a later acc
// definition while relying on this original definition still being emitted.
void FoldStoredExpressions(const AstMethod& method, SourceAst& ast,
                           std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
