#ifndef ABCD_SOURCE_EXPRESSION_CONSTANTS_H_
#define ABCD_SOURCE_EXPRESSION_CONSTANTS_H_

#include <span>

#include "abcd/source/source_tree.h"

namespace abcd::source {
// Constants are resolved by the ABC adapter before syntax construction. Pool
// indices never become executable tree operands. Entries are indexed by OpId.
struct ObjectConstant { std::uint32_t first; std::uint32_t count; };
struct ConstantProperty { NameId key; TreeLiteral value; };
using ExpressionConstant = std::variant<std::monostate, NameId, TextId, ObjectConstant>;
struct ExpressionConstants {
  std::span<const ExpressionConstant> operations;
  std::size_t name_count = 0;
  std::size_t text_count = 0;
  std::optional<TextId> number_type_text;
  std::span<const ConstantProperty> properties;
};
}  // namespace abcd::source
#endif
