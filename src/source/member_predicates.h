#ifndef ABCD_SOURCE_MEMBER_PREDICATES_H_
#define ABCD_SOURCE_MEMBER_PREDICATES_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// One selected predicate slot per method: bounded final-AST may-liveness.
std::optional<SourceFailure> FoldMemberPredicates(
    const AstMethod& method, SourceAst& ast, std::size_t budget_bytes);
}  // namespace abcd::source
#endif
