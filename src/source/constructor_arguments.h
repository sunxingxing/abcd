#ifndef ABCD_SOURCE_CONSTRUCTOR_ARGUMENTS_H_
#define ABCD_SOURCE_CONSTRUCTOR_ARGUMENTS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
void RecoverConstructorArguments(const AstMethod& method, SourceAst& ast,
                                 std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_CONSTRUCTOR_ARGUMENTS_H_
