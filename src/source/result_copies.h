#ifndef ABCD_SOURCE_RESULT_COPIES_H_
#define ABCD_SOURCE_RESULT_COPIES_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Retarget a uniquely consumed saved result through an emitted private copy.
// Run before physical accumulator tracking; preserve original IR/SSA and PCs.
void FoldResultCopies(const AstMethod& method, SourceAst& ast,
                      std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
