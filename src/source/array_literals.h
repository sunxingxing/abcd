#ifndef ABCD_SOURCE_ARRAY_LITERALS_H_
#define ABCD_SOURCE_ARRAY_LITERALS_H_
#include "semantic_ast_input.h"
namespace abcd::source {
// Fold unobserved same-block dense array construction, not arbitrary indexed
// assignment. Object buffers still require primitive-only printer validation.
std::optional<SourceFailure> FoldArrayLiterals(
    const AstMethod& method, SourceAst& ast, std::size_t bytes);
}  // namespace abcd::source
#endif
