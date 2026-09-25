#ifndef ABCD_SOURCE_STRING_COMPARISON_H_
#define ABCD_SOURCE_STRING_COMPARISON_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Adjacent single-use string loads into strict comparisons only. Original
// use counts include exceptional observers; run before branch folding.
void FoldStringComparisons(const AstMethod& method, SourceAst& ast,
                           std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
