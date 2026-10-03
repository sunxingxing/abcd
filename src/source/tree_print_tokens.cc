#include "tree_print_tokens.h"

#include "abcd/source/tree_printer.h"

namespace abcd::source {
BinarySyntax TreeBinarySyntax(analysis::BinaryOperator operation) {
  using Op = analysis::BinaryOperator;
  using P = ExpressionPrecedence;
  switch (operation) {
    case Op::kAdd: return {" + ", P::kAdditive};
    case Op::kSubtract: return {" - ", P::kAdditive};
    case Op::kMultiply: return {" * ", P::kMultiplicative};
    case Op::kDivide: return {" / ", P::kMultiplicative};
    case Op::kRemainder: return {" % ", P::kMultiplicative};
    case Op::kExponent: return {" ** ", P::kExponent, OperatorAssociativity::kRight};
    case Op::kEqual: return {" == ", P::kEquality};
    case Op::kNotEqual: return {" != ", P::kEquality};
    case Op::kLess: return {" < ", P::kRelational};
    case Op::kLessEqual: return {" <= ", P::kRelational};
    case Op::kGreater: return {" > ", P::kRelational};
    case Op::kGreaterEqual: return {" >= ", P::kRelational};
    case Op::kShiftLeft: return {" << ", P::kShift};
    case Op::kShiftRightUnsigned: return {" >>> ", P::kShift};
    case Op::kShiftRightSigned: return {" >> ", P::kShift};
    case Op::kBitAnd: return {" & ", P::kBitAnd};
    case Op::kBitOr: return {" | ", P::kBitOr};
    case Op::kBitXor: return {" ^ ", P::kBitXor};
    case Op::kStrictEqual: return {" === ", P::kEquality};
    case Op::kStrictNotEqual: return {" !== ", P::kEquality};
    case Op::kIn: return {" in ", P::kRelational};
    case Op::kInstanceOf: return {" instanceof ", P::kRelational};
  }
  throw TreePrintError::kUnsupportedContext;
}
namespace {
ExpressionPrecedence LogicalPrecedence(TreeLogical op) {
  switch (op) {
    case TreeLogical::kAnd: return ExpressionPrecedence::kLogicalAnd;
    case TreeLogical::kOr: case TreeLogical::kNullish: return ExpressionPrecedence::kLogicalOr;
  }
  throw TreePrintError::kUnsupportedContext;
}
ExpressionPrecedence Precedence(const TreeExpression& node) {
  if (const auto* binary = std::get_if<BinaryExpr>(&node)) return TreeBinarySyntax(binary->operation).precedence;
  if (std::holds_alternative<UnaryExpr>(node)) return ExpressionPrecedence::kUnary;
  if (const auto* logical = std::get_if<LogicalExpr>(&node)) return LogicalPrecedence(logical->operation);
  if (std::holds_alternative<MemberExpr>(node) || std::holds_alternative<CallExpr>(node) ||
      std::holds_alternative<ConstructExpr>(node) || std::holds_alternative<SuperCallExpr>(node))
    return ExpressionPrecedence::kPostfix;
  // Other composite forms still print their own grouping. Primary leaves
  // need none; never infer precedence by examining printed text.
  return ExpressionPrecedence::kPrimary;
}
bool Sign(TreeUnary op) { return op == TreeUnary::kPlus || op == TreeUnary::kNegate; }
}
bool TreeOperandGrouping(const TreeExpression& child, const TreeOperatorContext& context) {
  if (std::holds_alternative<std::monostate>(context.parent)) return false;
  const auto child_precedence = Precedence(child);
  return std::visit([&](const auto& parent) -> bool {
    using T = std::decay_t<decltype(parent)>;
    if constexpr (std::is_same_v<T, analysis::BinaryOperator>) {
      const auto syntax = TreeBinarySyntax(parent);
      if (parent == analysis::BinaryOperator::kExponent && context.side == OperandSide::kLeft &&
          std::holds_alternative<UnaryExpr>(child)) return true;
      if (child_precedence != syntax.precedence) return child_precedence < syntax.precedence;
      // Equal-precedence grouping preserves the actual AST, including floating
      // arithmetic and coercion order. Do not assume algebraic associativity.
      return syntax.associativity == OperatorAssociativity::kLeft ?
          context.side == OperandSide::kRight : context.side == OperandSide::kLeft;
    } else if constexpr (std::is_same_v<T, TreeUnary>) {
      if (const auto* unary = std::get_if<UnaryExpr>(&child); unary && Sign(parent) && Sign(unary->operation)) return true;
      return child_precedence < ExpressionPrecedence::kUnary;
    } else if constexpr (std::is_same_v<T, TreeLogical>) {
      if (const auto* logical = std::get_if<LogicalExpr>(&child); logical &&
          ((parent == TreeLogical::kNullish) != (logical->operation == TreeLogical::kNullish))) return true;
      const auto precedence = LogicalPrecedence(parent);
      return child_precedence < precedence || (child_precedence == precedence && context.side == OperandSide::kRight);
    } else if constexpr (std::is_same_v<T, PostfixContext>) {
      if (parent == PostfixContext::kConstructor &&
          (std::holds_alternative<CallExpr>(child) || std::holds_alternative<SuperCallExpr>(child))) return true;
      // An integer's decimal dot belongs to NumericLiteral (1.value is not a
      // member access). Group numeric bases regardless of current formatting.
      if (parent != PostfixContext::kCallCallee) if (const auto* literal = std::get_if<TreeLiteral>(&child);
          literal && (std::holds_alternative<IntegerLiteral>(*literal) || std::holds_alternative<NumberLiteral>(*literal))) return true;
      return child_precedence < ExpressionPrecedence::kPostfix;
    } else if constexpr (std::is_same_v<T, ExpressionPrecedence>) return child_precedence < parent;
    else return false;
  }, context.parent);
}
std::string_view TreeBinaryToken(analysis::BinaryOperator operation) { return TreeBinarySyntax(operation).token; }
std::string_view TreeUnaryToken(TreeUnary operation) {
  switch (operation) {
    case TreeUnary::kNot: return "!";
    case TreeUnary::kNegate: return "-";
    case TreeUnary::kBitNot: return "~";
    case TreeUnary::kPlus: return "+";
    case TreeUnary::kTypeof: return "typeof ";
  }
  throw TreePrintError::kUnsupportedContext;
}
std::string_view TreeLogicalToken(TreeLogical operation) {
  switch (operation) {
    case TreeLogical::kAnd: return " && ";
    case TreeLogical::kOr: return " || ";
    case TreeLogical::kNullish: return " ?? ";
  }
  throw TreePrintError::kUnsupportedContext;
}
}  // namespace abcd::source
