#ifndef ABCD_SOURCE_BRANCH_PREDICATE_H_
#define ABCD_SOURCE_BRANCH_PREDICATE_H_

#include "semantic_ast_input.h"

namespace abcd::source {
class CopyOrigins;
// Refine VM branch tests only from exact SSA boolean definitions. No value
// movement, graph rewrite, phi speculation or new storage is permitted.
void RefineBranchPredicates(const AstMethod& method, SourceAst& ast,
                            const CopyOrigins& copies);
// Adjacent, pure, single-use boolean producers become predicate inputs;
// use counts must include exceptional phi observations.
void FoldBranchExpressions(const AstMethod& method, SourceAst& ast,
                           std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
