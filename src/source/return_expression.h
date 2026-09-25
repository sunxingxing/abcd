#ifndef ABCD_SOURCE_RETURN_EXPRESSION_H_
#define ABCD_SOURCE_RETURN_EXPRESSION_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Fuse an adjacent single-use acc definition/return without moving evaluation.
// Handler phi uses are included in uses. Both original PCs remain mapped.
// Proven member-call read/spill/reload origins stay attached to the return.
void FoldReturnExpressions(const AstMethod& method, SourceAst& ast,
                           std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
