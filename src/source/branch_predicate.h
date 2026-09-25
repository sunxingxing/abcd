#ifndef ABCD_SOURCE_BRANCH_PREDICATE_H_
#define ABCD_SOURCE_BRANCH_PREDICATE_H_

#include "semantic_ast_input.h"

namespace abcd::source {
class CopyOrigins;
// Refine VM branch tests only from exact SSA boolean definitions. No value
// movement, graph rewrite, phi speculation or new storage is permitted.
void RefineBranchPredicates(const AstMethod& method, SourceAst& ast,
                            const CopyOrigins& copies);
}  // namespace abcd::source
#endif
