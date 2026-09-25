#ifndef ABCD_SOURCE_FIELD_READS_H_
#define ABCD_SOURCE_FIELD_READS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Final AST liveness certifies acc is dead after an adjacent named store.
std::optional<SourceFailure> FoldFieldReads(
    const AstMethod& method, SourceAst& ast, std::size_t budget_bytes);
}  // namespace abcd::source
#endif
