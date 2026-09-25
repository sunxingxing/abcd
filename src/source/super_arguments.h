#ifndef ABCD_SOURCE_SUPER_ARGUMENTS_H_
#define ABCD_SOURCE_SUPER_ARGUMENTS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// One original accumulator load and its adjacent register save per argument.
inline constexpr std::size_t kSuperArgumentCopyInstructions = 2;
// The derived owner admits one super. Keep the original AST nodes and origins;
// the printer consumes their private reads in the native argument list.
std::optional<SuperArgumentRegion> ProveSuperArguments(
    const AstMethod& method, const SourceAst& ast,
    std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
