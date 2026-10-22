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
std::optional<analysis::BinaryOperator> TreeEqualityComplement(analysis::BinaryOperator operation) {
  using Op = analysis::BinaryOperator;
  switch (operation) {
    case Op::kEqual: return Op::kNotEqual;
    case Op::kNotEqual: return Op::kEqual;
    case Op::kStrictEqual: return Op::kStrictNotEqual;
    case Op::kStrictNotEqual: return Op::kStrictEqual;
    case Op::kAdd: case Op::kSubtract: case Op::kMultiply: case Op::kDivide:
    case Op::kRemainder: case Op::kExponent: case Op::kLess: case Op::kLessEqual:
    case Op::kGreater: case Op::kGreaterEqual: case Op::kShiftLeft:
    case Op::kShiftRightUnsigned: case Op::kShiftRightSigned: case Op::kBitAnd:
    case Op::kBitOr: case Op::kBitXor: case Op::kIn: case Op::kInstanceOf: return {};
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
    else if constexpr (std::is_same_v<T, ExpressionGrammar>) {
      switch (parent) {
        case ExpressionGrammar::kAssignmentValue: case ExpressionGrammar::kExpression:
        case ExpressionGrammar::kConditionalTest: case ExpressionGrammar::kUnknown: return false;
      }
      throw TreePrintError::kUnsupportedContext;
    } else {
      static_assert(std::is_same_v<T, std::monostate>);
      return false;
    }
  }, context.parent);
}
ExpressionPrecedence TreeExpressionPrecedence(const TreeExpression& node) {
  using P = ExpressionPrecedence;
  return std::visit([&](const auto& value) -> P {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, BinaryExpr>) return TreeBinarySyntax(value.operation).precedence;
    else if constexpr (std::is_same_v<T, LogicalExpr>) return LogicalPrecedence(value.operation);
    else if constexpr (std::is_same_v<T, SequenceExpr>) return P::kSequence;
    else if constexpr (std::is_same_v<T, AssignmentExpr> || std::is_same_v<T, YieldExpr> ||
                       std::is_same_v<T, FunctionExpr> || std::is_same_v<T, ClassExpr>) return P::kAssignment;
    else if constexpr (std::is_same_v<T, ConditionalExpr>) return P::kConditional;
    else if constexpr (std::is_same_v<T, UnaryExpr> || std::is_same_v<T, AwaitExpr> ||
                       std::is_same_v<T, DeleteExpr>) return P::kUnary;
    else if constexpr (std::is_same_v<T, MemberMutationExpr>) {
      const auto* update=std::get_if<MemberIncrement>(&value.operation);
      return !update?P::kAssignment:update->result==UpdateResult::kPostfix?P::kPostfix:P::kUnary;
    }
    else if constexpr (std::is_same_v<T, UpdateExpr>) return value.result == UpdateResult::kPostfix ? P::kPostfix : P::kUnary;
    else if constexpr (std::is_same_v<T, MemberExpr> || std::is_same_v<T, CallExpr> ||
                       std::is_same_v<T, ConstructExpr> || std::is_same_v<T, SuperCallExpr>) return P::kPostfix;
    else if constexpr (std::is_same_v<T, FieldDefinitionExpr> || std::is_same_v<T, ArrayElement>) return P::kLowest;
    else {
      static_assert((std::is_same_v<T, RegExpExpr> || std::is_same_v<T, TemplateObjectExpr>) || std::is_same_v<T, ObjectRestExpr> || std::is_same_v<T, PatternFailureExpr> || std::is_same_v<T, NewTargetExpr> || std::is_same_v<T, BigIntExpr> || std::is_same_v<T, TreeLiteral> || std::is_same_v<T, SymbolExpr> ||
          std::is_same_v<T, GlobalNameExpr> || std::is_same_v<T, RealmObjectExpr> ||
          std::is_same_v<T, SymbolConstructorExpr> || std::is_same_v<T, ThisExpr> ||
          std::is_same_v<T, EmptyArrayExpr> || std::is_same_v<T, EmptyObjectExpr> ||
          std::is_same_v<T, ObjectExpr> ||
          std::is_same_v<T, ArgumentSnapshotExpr> || std::is_same_v<T, ArrayExpr>);
      return P::kPrimary;
    }
  }, node);
}
bool ReadableOperandGrouping(const TreeExpression& child, const TreeOperatorContext& context) {
  const auto precedence = TreeExpressionPrecedence(child);
  return std::visit([&](const auto& parent) {
    using T = std::decay_t<decltype(parent)>;
    if constexpr (std::is_same_v<T, std::monostate>) return true; // Unknown use retains grouping.
    else if constexpr (std::is_same_v<T, ExpressionGrammar>) {
      switch(parent) {
        case ExpressionGrammar::kExpression: return false;
        case ExpressionGrammar::kAssignmentValue: return precedence < ExpressionPrecedence::kAssignment;
        case ExpressionGrammar::kConditionalTest: return precedence <= ExpressionPrecedence::kConditional;
        case ExpressionGrammar::kUnknown: return true;
      }
      return true;
    } else if constexpr (std::is_same_v<T, ExpressionPrecedence>) return precedence < parent;
    else if constexpr (std::is_same_v<T, analysis::BinaryOperator>) {
      const auto syntax = TreeBinarySyntax(parent);
      if(parent == analysis::BinaryOperator::kExponent && context.side == OperandSide::kLeft &&
          (std::holds_alternative<UnaryExpr>(child) || std::holds_alternative<AwaitExpr>(child) ||
           std::holds_alternative<DeleteExpr>(child))) return true;
      if(precedence != syntax.precedence) return precedence < syntax.precedence;
      return syntax.associativity == OperatorAssociativity::kLeft ?
          context.side == OperandSide::kRight : context.side == OperandSide::kLeft;
    } else if constexpr (std::is_same_v<T, TreeLogical>) {
      if(const auto* logical=std::get_if<LogicalExpr>(&child);logical &&
          ((parent==TreeLogical::kNullish)!=(logical->operation==TreeLogical::kNullish))) return true;
      const auto p=LogicalPrecedence(parent);
      return precedence<p || (precedence==p && context.side==OperandSide::kRight);
    } else if constexpr (std::is_same_v<T, TreeUnary>) {
      if(const auto* unary=std::get_if<UnaryExpr>(&child);unary && Sign(parent) && Sign(unary->operation)) return true;
      // A prefix update must not join a unary sign into +++ or --- tokens.
      if (const auto* update = std::get_if<UpdateExpr>(&child);
          update && update->result == UpdateResult::kPrefix && Sign(parent)) return true;
      if(const auto* member=std::get_if<MemberMutationExpr>(&child); member && Sign(parent))
        if(const auto* update=std::get_if<MemberIncrement>(&member->operation);update && update->result==UpdateResult::kPrefix)return true;
      return precedence<ExpressionPrecedence::kUnary;
    } else {
      static_assert(std::is_same_v<T, PostfixContext>);
      // UpdateExpression is not a MemberExpression/CallExpression base.
      if (std::holds_alternative<UpdateExpr>(child)||std::holds_alternative<MemberMutationExpr>(child)) return true;
      if(parent==PostfixContext::kConstructor &&
          (std::holds_alternative<CallExpr>(child)||std::holds_alternative<SuperCallExpr>(child))) return true;
      if(parent!=PostfixContext::kCallCallee) if(const auto* literal=std::get_if<TreeLiteral>(&child);literal &&
          (std::holds_alternative<IntegerLiteral>(*literal)||std::holds_alternative<NumberLiteral>(*literal))) return true;
      return precedence<ExpressionPrecedence::kPostfix;
    }
  },context.parent);
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
