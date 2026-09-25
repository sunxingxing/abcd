#ifndef ABCD_SOURCE_STORED_CALLS_H_
#define ABCD_SOURCE_STORED_CALLS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
void FoldStoredCalls(const AstMethod& method, SourceAst& ast,
                     std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
