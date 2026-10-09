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
// Callable ownership is fixed by the checked tree's FunctionOwnerId. Local
// scope projection cannot change it; initialization/effects are separate facts.
enum class LexicalDependency : std::uint8_t { kReferences, kCallableOwner, kEnclosingEnvironment };
inline LexicalDependency IntrinsicLexicalDependency(const TreeExpression& expression) {
  return std::visit([](const auto& node) {
    using T=std::decay_t<decltype(node)>;
    if constexpr(std::is_same_v<T,ThisExpr>) return LexicalDependency::kCallableOwner;
    else if constexpr(std::is_same_v<T,FunctionExpr> ||
        std::is_same_v<T,ClassExpr> || std::is_same_v<T,RealmObjectExpr> ||
        std::is_same_v<T,SymbolConstructorExpr> || std::is_same_v<T,ArgumentSnapshotExpr> ||
        std::is_same_v<T,SuperCallExpr> || std::is_same_v<T,FieldDefinitionExpr>) return LexicalDependency::kEnclosingEnvironment;
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
inline analysis::ValueKinds PrimitiveKinds(const TreeLiteral& literal) {
  using K=analysis::ValueKinds;
  return std::visit([](const auto& value) {
    using T=std::decay_t<decltype(value)>;
    if constexpr(std::is_same_v<T,IntegerLiteral>||std::is_same_v<T,NumberLiteral>)return K::Number();
    else if constexpr(std::is_same_v<T,BooleanLiteral>)return K::Boolean();
    else if constexpr(std::is_same_v<T,TextId>)return K::String();
    else if constexpr(std::is_same_v<T,NullLiteral>)return K::ZeroOrOnePrimitive();
    else {static_assert(std::is_same_v<T,UndefinedLiteral>);return K::Language();}
  },literal);
}
inline bool CoercionPrimitive(analysis::ValueKinds kinds) {
  return kinds.IsNumber()||kinds.IsString()||kinds.IsZeroOrOnePrimitive();
}
// One exhaustive ECMA-262 effect table for canonical and projected source.
// Normal operand domains may exclude coercion hooks/language exceptions;
// GetValue, lexical initialization and child completion are separate facts.
// Undefined/Symbol/BigInt lack precise ValueKinds categories and remain unknown.
inline ExpressionEvaluation NormalValueEvaluation(const TreeExpression& expression,
    std::span<const analysis::ValueKinds> kinds,ResourceFailureScope resources) {
  using E=ExpressionEvaluation;
  return std::visit([&](const auto& node)->E {
    using T=std::decay_t<decltype(node)>;
    if constexpr(std::is_same_v<T,TreeLiteral>||std::is_same_v<T,SequenceExpr>||
        std::is_same_v<T,LogicalExpr>||std::is_same_v<T,ConditionalExpr>)return E::kInert;
    else if constexpr(std::is_same_v<T,UnaryExpr>) {
      const bool primitive=!kinds.empty()&&CoercionPrimitive(kinds[node.operand.value]);
      switch(node.operation) {
        case TreeUnary::kNot:case TreeUnary::kTypeof:return E::kInert;
        case TreeUnary::kNegate:case TreeUnary::kBitNot:case TreeUnary::kPlus:
          return primitive?E::kInert:E::kRequired;
      }
    } else if constexpr(std::is_same_v<T,BinaryExpr>) {
      const auto left=kinds.empty()?analysis::ValueKinds::Language():kinds[node.left.value];
      const auto right=kinds.empty()?analysis::ValueKinds::Language():kinds[node.right.value];
      const bool primitive=CoercionPrimitive(left)&&CoercionPrimitive(right);
      switch(node.operation) {
        case analysis::BinaryOperator::kStrictEqual:case analysis::BinaryOperator::kStrictNotEqual:return E::kInert;
        case analysis::BinaryOperator::kAdd:
          if(resources==ResourceFailureScope::kObservable&&(left.IsString()||right.IsString()))return E::kRequired;
          return primitive?E::kInert:E::kRequired;
        case analysis::BinaryOperator::kSubtract:case analysis::BinaryOperator::kMultiply:
        case analysis::BinaryOperator::kDivide:case analysis::BinaryOperator::kRemainder:
        case analysis::BinaryOperator::kExponent:case analysis::BinaryOperator::kEqual:
        case analysis::BinaryOperator::kNotEqual:case analysis::BinaryOperator::kLess:
        case analysis::BinaryOperator::kLessEqual:case analysis::BinaryOperator::kGreater:
        case analysis::BinaryOperator::kGreaterEqual:case analysis::BinaryOperator::kShiftLeft:
        case analysis::BinaryOperator::kShiftRightSigned:case analysis::BinaryOperator::kShiftRightUnsigned:
        case analysis::BinaryOperator::kBitAnd:case analysis::BinaryOperator::kBitOr:
        case analysis::BinaryOperator::kBitXor:return primitive?E::kInert:E::kRequired;
        case analysis::BinaryOperator::kIn:case analysis::BinaryOperator::kInstanceOf:return E::kRequired;
      }
    } else {
      static_assert(std::is_same_v<T,SymbolExpr>||std::is_same_v<T,GlobalNameExpr>||
          std::is_same_v<T,RealmObjectExpr>||std::is_same_v<T,SymbolConstructorExpr>||
          std::is_same_v<T,ThisExpr>||std::is_same_v<T,MemberExpr>||
          std::is_same_v<T,AwaitExpr>||std::is_same_v<T,YieldExpr>||
          std::is_same_v<T,AssignmentExpr>||std::is_same_v<T,UpdateExpr>||
          std::is_same_v<T,CallExpr>||std::is_same_v<T,ConstructExpr>||
          std::is_same_v<T,EmptyArrayExpr>||std::is_same_v<T,EmptyObjectExpr>||
          std::is_same_v<T,ObjectExpr>||std::is_same_v<T,FunctionExpr>||
          std::is_same_v<T,ArgumentSnapshotExpr>||std::is_same_v<T,ClassExpr>||
          std::is_same_v<T,FieldDefinitionExpr>||std::is_same_v<T,SuperCallExpr>);
    }
    return E::kRequired;
  },expression);
}
inline ExpressionEvaluation IntrinsicEvaluation(const TreeExpression& expression) {
  return NormalValueEvaluation(expression,{},ResourceFailureScope::kObservable);
}
}
#endif
