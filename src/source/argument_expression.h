#ifndef ABCD_SOURCE_ARGUMENT_EXPRESSION_H_
#define ABCD_SOURCE_ARGUMENT_EXPRESSION_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Final AST annotation: the ordered native argument owns the sole SSA use.
// Counts include phi inputs. Erased same-register copy chains may connect the
// producer to that use; every intermediate must be sole-use. No effect moves.
void FoldArgumentExpressions(const AstMethod& method, SourceAst& ast,
                             std::span<const std::size_t> uses);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_ARGUMENT_EXPRESSION_H_
