#ifndef ABCD_SOURCE_PREDICATE_COPIES_H_
#define ABCD_SOURCE_PREDICATE_COPIES_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Run after branch and stored-expression folding, before reload elision.
// Original use counts include handler phis; never cross an emitted operation.
void FoldPredicateCopies(const AstMethod& method, SourceAst& ast,
                         std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
