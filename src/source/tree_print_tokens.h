#ifndef ABCD_SOURCE_TREE_PRINT_TOKENS_H_
#define ABCD_SOURCE_TREE_PRINT_TOKENS_H_

#include <string_view>

#include "abcd/source/source_tree.h"

namespace abcd::source {
// Textual boundary only; semantic decisions remain in the typed tree.
[[nodiscard]] std::string_view TreeBinaryToken(analysis::BinaryOperator operation);
[[nodiscard]] std::string_view TreeUnaryToken(TreeUnary operation);
[[nodiscard]] std::string_view TreeLogicalToken(TreeLogical operation);
}  // namespace abcd::source
#endif
