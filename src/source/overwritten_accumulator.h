#ifndef ABCD_SOURCE_OVERWRITTEN_ACCUMULATOR_H_
#define ABCD_SOURCE_OVERWRITTEN_ACCUMULATOR_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Final emitted-AST cleanup, not original-SSA DCE. O(I), constant scratch.
// Keep PC anchors and exclude owned argument intervals and merged reads.
void ElideOverwrittenAccumulator(const AstMethod& method, SourceAst& ast);
}  // namespace abcd::source
#endif
