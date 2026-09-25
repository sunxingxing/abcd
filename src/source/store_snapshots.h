#ifndef ABCD_SOURCE_STORE_SNAPSHOTS_H_
#define ABCD_SOURCE_STORE_SNAPSHOTS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Final-AST local copy propagation; no original SSA liveness assumptions.
std::optional<SourceFailure> FoldStoreSnapshots(
    const AstMethod& method, SourceAst& ast, std::size_t budget_bytes);
}  // namespace abcd::source
#endif
