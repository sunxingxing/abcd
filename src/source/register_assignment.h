#ifndef ABCD_SOURCE_REGISTER_ASSIGNMENT_H_
#define ABCD_SOURCE_REGISTER_ASSIGNMENT_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Redirect a single-use acc expression into its adjacent private register
// destination. The original use counts must include exceptional phi inputs.
// Run before identity-based reload elision: that pass can remove a later acc
// definition while relying on this original definition still being emitted.
void FoldRegisterAssignments(const AstMethod& method, SourceAst& ast,
                             std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
