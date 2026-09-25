#ifndef ABCD_SOURCE_STORE_SNAPSHOTS_H_
#define ABCD_SOURCE_STORE_SNAPSHOTS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Final-AST local copy propagation; no original SSA liveness assumptions.
void FoldStoreSnapshots(const AstMethod& method, SourceAst& ast);
}  // namespace abcd::source
#endif
