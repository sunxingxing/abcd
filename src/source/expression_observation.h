#ifndef ABCD_SOURCE_EXPRESSION_OBSERVATION_H_
#define ABCD_SOURCE_EXPRESSION_OBSERVATION_H_
#include "abcd/source/source_tree.h"
#include <type_traits>
namespace abcd::source {
enum class ExpressionEvaluation : std::uint8_t { kRequired, kInert };
// Explicit binding and free-name references can be checked at a destination
// scope. Closure environments and implicit source helpers still require the
// enclosing environment until their dependencies join that reference model.
// This exhaustive AST semantic property is not an opcode admission list.
enum class LexicalDependency : std::uint8_t { kReferences, kEnclosingEnvironment };
inline LexicalDependency IntrinsicLexicalDependency(const TreeExpression& expression) {
  return std::visit([](const auto& node) {
    using T=std::decay_t<decltype(node)>;
    if constexpr(std::is_same_v<T,FunctionExpr> ||
        std::is_same_v<T,ClassExpr> || std::is_same_v<T,RealmObjectExpr> ||
        std::is_same_v<T,SymbolConstructorExpr> || std::is_same_v<T,ArgumentSnapshotExpr> ||
        std::is_same_v<T,SuperCallExpr> || std::is_same_v<T,FieldDefinitionExpr> ||
        std::is_same_v<T,ThisExpr>) return LexicalDependency::kEnclosingEnvironment;
    else {
      static_assert(std::is_same_v<T,TreeLiteral> || std::is_same_v<T,SymbolExpr> ||
          std::is_same_v<T,GlobalNameExpr> ||
          std::is_same_v<T,MemberExpr> || std::is_same_v<T,UnaryExpr> ||
          std::is_same_v<T,AwaitExpr> || std::is_same_v<T,YieldExpr> ||
          std::is_same_v<T,BinaryExpr> || std::is_same_v<T,LogicalExpr> ||
          std::is_same_v<T,ConditionalExpr> || std::is_same_v<T,AssignmentExpr> ||
          std::is_same_v<T,UpdateExpr> || std::is_same_v<T,SequenceExpr> ||
          std::is_same_v<T,CallExpr> || std::is_same_v<T,ConstructExpr> ||
          std::is_same_v<T,EmptyArrayExpr> || std::is_same_v<T,EmptyObjectExpr> ||
          std::is_same_v<T,ObjectExpr>);
      return LexicalDependency::kReferences;
    }
  },expression);
}
// ECMA-262 2024 unary/binary evaluation: operands here are already values.
// ToBoolean/typeof/strict equality have no user hooks. Coercing operators may
// throw or call user code, so unknown values retain evaluation. This exhaustive
// language table is independent of opcode/profile/compiler layout. Child
// completion and binding availability are proved separately by the census.
inline ExpressionEvaluation IntrinsicEvaluation(const TreeExpression& expression) {
  using Evaluation = ExpressionEvaluation;
  return std::visit([](const auto& node) -> Evaluation {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, TreeLiteral> || std::is_same_v<T, SequenceExpr> ||
        std::is_same_v<T, LogicalExpr> || std::is_same_v<T, ConditionalExpr>) return Evaluation::kInert;
    else if constexpr (std::is_same_v<T, UnaryExpr>) {
      switch (node.operation) {
        case TreeUnary::kNot: case TreeUnary::kTypeof: return Evaluation::kInert;
        case TreeUnary::kNegate: case TreeUnary::kBitNot: case TreeUnary::kPlus: return Evaluation::kRequired;
      }
    } else if constexpr (std::is_same_v<T, BinaryExpr>) {
      switch (node.operation) {
        case analysis::BinaryOperator::kStrictEqual: case analysis::BinaryOperator::kStrictNotEqual:
          return Evaluation::kInert;
        case analysis::BinaryOperator::kAdd: case analysis::BinaryOperator::kSubtract:
        case analysis::BinaryOperator::kMultiply: case analysis::BinaryOperator::kDivide:
        case analysis::BinaryOperator::kRemainder: case analysis::BinaryOperator::kExponent:
        case analysis::BinaryOperator::kEqual: case analysis::BinaryOperator::kNotEqual:
        case analysis::BinaryOperator::kLess: case analysis::BinaryOperator::kLessEqual:
        case analysis::BinaryOperator::kGreater: case analysis::BinaryOperator::kGreaterEqual:
        case analysis::BinaryOperator::kShiftLeft: case analysis::BinaryOperator::kShiftRightSigned:
        case analysis::BinaryOperator::kShiftRightUnsigned: case analysis::BinaryOperator::kBitAnd:
        case analysis::BinaryOperator::kBitOr: case analysis::BinaryOperator::kBitXor:
        case analysis::BinaryOperator::kIn: case analysis::BinaryOperator::kInstanceOf:
          return Evaluation::kRequired;
      }
    } else {
      static_assert(std::is_same_v<T, SymbolExpr> || std::is_same_v<T, GlobalNameExpr> ||
          std::is_same_v<T, RealmObjectExpr> || std::is_same_v<T, SymbolConstructorExpr> ||
          std::is_same_v<T, ThisExpr> || std::is_same_v<T, MemberExpr> ||
          std::is_same_v<T, AwaitExpr> || std::is_same_v<T, YieldExpr> ||
          std::is_same_v<T, AssignmentExpr> || std::is_same_v<T, UpdateExpr> ||
          std::is_same_v<T, CallExpr> || std::is_same_v<T, ConstructExpr> ||
          std::is_same_v<T, EmptyArrayExpr> || std::is_same_v<T, EmptyObjectExpr> ||
          std::is_same_v<T, ObjectExpr> || std::is_same_v<T, FunctionExpr> ||
          std::is_same_v<T, ArgumentSnapshotExpr> || std::is_same_v<T, ClassExpr> ||
          std::is_same_v<T, FieldDefinitionExpr> || std::is_same_v<T, SuperCallExpr>);
    }
    return Evaluation::kRequired;
  }, expression);
}
}
#endif
