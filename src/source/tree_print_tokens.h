#ifndef ABCD_SOURCE_TREE_PRINT_TOKENS_H_
#define ABCD_SOURCE_TREE_PRINT_TOKENS_H_

#include <optional>
#include <string_view>

#include "abcd/source/source_tree.h"

namespace abcd::source {
// Ordered by ECMAScript binding strength. Grammar exceptions (exponent bases,
// adjacent signs and nullish/logical mixing) are checked separately.
enum class ExpressionPrecedence : std::uint8_t {
  kLowest, kLogicalOr, kLogicalAnd, kBitOr, kBitXor, kBitAnd, kEquality,
  kRelational, kShift, kAdditive, kMultiplicative, kExponent, kUnary, kPostfix, kPrimary
};
enum class OperatorAssociativity : std::uint8_t { kLeft, kRight };
enum class OperandSide : std::uint8_t { kLeft, kRight };
struct BinarySyntax {
  std::string_view token;
  ExpressionPrecedence precedence;
  OperatorAssociativity associativity = OperatorAssociativity::kLeft;
};
// ECMA-262 Left-Hand-Side Expressions: new requires MemberExpression, while
// call/member chains may include CallExpression. Propagate kConstructor down
// member bases so a factory call is grouped without guessing printed tokens.
enum class PostfixContext : std::uint8_t { kMemberBase, kCallCallee, kConstructor };
struct TreeOperatorContext {
  std::variant<std::monostate, analysis::BinaryOperator, TreeUnary, TreeLogical, ExpressionPrecedence, PostfixContext> parent;
  OperandSide side = OperandSide::kLeft;
};
[[nodiscard]] BinarySyntax TreeBinarySyntax(analysis::BinaryOperator);
// Equality operators are exact Boolean complements, including conversions and
// abrupt completion. Relational comparisons are not complements in the NaN
// domain; absence retains the original NOT. ECMA-262 2024 13.10/13.11.
[[nodiscard]] std::optional<analysis::BinaryOperator> TreeEqualityComplement(analysis::BinaryOperator);
[[nodiscard]] bool TreeOperandGrouping(const TreeExpression&, const TreeOperatorContext&);
// Textual boundary only; semantic decisions remain in the typed tree.
[[nodiscard]] std::string_view TreeBinaryToken(analysis::BinaryOperator operation);
[[nodiscard]] std::string_view TreeUnaryToken(TreeUnary operation);
[[nodiscard]] std::string_view TreeLogicalToken(TreeLogical operation);
}  // namespace abcd::source
#endif
