#ifndef ABCD_SOURCE_TREE_PRINT_TOKENS_H_
#define ABCD_SOURCE_TREE_PRINT_TOKENS_H_

#include <string_view>

#include "abcd/source/source_tree.h"

namespace abcd::source {
// Ordered by ECMAScript binding strength. Grammar exceptions (exponent bases,
// adjacent signs and nullish/logical mixing) are checked separately.
enum class ExpressionPrecedence : std::uint8_t {
  kLowest, kLogicalOr, kLogicalAnd, kBitOr, kBitXor, kBitAnd, kEquality,
  kRelational, kShift, kAdditive, kMultiplicative, kExponent, kUnary, kPrimary
};
enum class OperatorAssociativity : std::uint8_t { kLeft, kRight };
enum class OperandSide : std::uint8_t { kLeft, kRight };
struct BinarySyntax {
  std::string_view token;
  ExpressionPrecedence precedence;
  OperatorAssociativity associativity = OperatorAssociativity::kLeft;
};
struct TreeOperatorContext {
  std::variant<std::monostate, analysis::BinaryOperator, TreeUnary, TreeLogical, ExpressionPrecedence> parent;
  OperandSide side = OperandSide::kLeft;
};
[[nodiscard]] BinarySyntax TreeBinarySyntax(analysis::BinaryOperator);
[[nodiscard]] bool TreeOperandGrouping(const TreeExpression&, const TreeOperatorContext&);
// Textual boundary only; semantic decisions remain in the typed tree.
[[nodiscard]] std::string_view TreeBinaryToken(analysis::BinaryOperator operation);
[[nodiscard]] std::string_view TreeUnaryToken(TreeUnary operation);
[[nodiscard]] std::string_view TreeLogicalToken(TreeLogical operation);
}  // namespace abcd::source
#endif
