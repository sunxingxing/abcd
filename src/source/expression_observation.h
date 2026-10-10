#ifndef ABCD_SOURCE_EXPRESSION_OBSERVATION_H_
#define ABCD_SOURCE_EXPRESSION_OBSERVATION_H_
#include "abcd/source/source_tree.h"
#include "tree_structure.h"
#include <type_traits>
namespace abcd::source {
enum class ExpressionEvaluation : std::uint8_t { kRequired, kInert };
// Intrinsic source operation effects, not child evaluations or successful
// declarative commits. The shared flow adapter owns those separate phases.
// Normal/before-throw masks describe possible effects on each completion;
// Required retention is independent of permission to mutate a captured unit.
struct SourceEffects {
  analysis::LanguageEffect normal=analysis::LanguageEffect::kNone;
  analysis::LanguageEffect before_throw=analysis::LanguageEffect::kNone;
  analysis::ExceptionalCompletion exceptional=analysis::ExceptionalCompletion::kImpossible;
  [[nodiscard]] ExpressionEvaluation Evaluation() const {
    return normal==analysis::LanguageEffect::kNone&&before_throw==analysis::LanguageEffect::kNone&&
        exceptional==analysis::ExceptionalCompletion::kImpossible?ExpressionEvaluation::kInert:ExpressionEvaluation::kRequired;
  }
};
inline bool HasLanguageEffect(analysis::LanguageEffect effects,analysis::LanguageEffect effect) {
  return (static_cast<std::uint16_t>(effects)&static_cast<std::uint16_t>(effect))!=0;
}
// Independent upper bounds for the SAME intrinsic action. UserCode includes
// arbitrary transitive effects; it is not a single disjoint effect bit.
inline SourceEffects IntersectSourceEffects(SourceEffects source,SourceEffects proof) {
  const auto mask=[](analysis::LanguageEffect a,analysis::LanguageEffect b) {
    if(HasLanguageEffect(a,analysis::LanguageEffect::kUserCode))return b;
    if(HasLanguageEffect(b,analysis::LanguageEffect::kUserCode))return a;
    return static_cast<analysis::LanguageEffect>(static_cast<std::uint16_t>(a)&static_cast<std::uint16_t>(b));
  };
  source.normal=mask(source.normal,proof.normal);
  source.before_throw=mask(source.before_throw,proof.before_throw);
  if(proof.exceptional==analysis::ExceptionalCompletion::kImpossible)
    source.exceptional=analysis::ExceptionalCompletion::kImpossible;
  return source;
}
inline SourceEffects CallbackEffects(analysis::LanguageEffect extra=analysis::LanguageEffect::kNone) {
  using L=analysis::LanguageEffect;
  const auto possible=L::kUserCode|L::kHeapRead|L::kHeapWrite|L::kAllocation|extra;
  return {possible,possible,analysis::ExceptionalCompletion::kMayThrow};
}
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
// ToNumeric followed by a numeric operation preserves the numeric category,
// not a finite/range proof. Unknown input may produce BigInt or invoke hooks;
// successful value domains do not authorize erasing those effects.
inline analysis::ValueKinds NumericValueKinds(analysis::ValueKinds input) {
  using K=analysis::ValueKinds;
  return input.IsEmpty()?K{}:CoercionPrimitive(input)?K::Number():K::Language();
}
// ECMA-262 normal results only. Bottom is an unresolved equation, never a
// language value. Unknown Number/BigInt and observable cells stay Language;
// initialization, effects and exceptional completion have independent owners.
inline analysis::ValueKinds NormalValueKinds(const TreeExpression& expression,
    std::span<const analysis::ValueKinds> kinds) {
  using K=analysis::ValueKinds;
  return std::visit([&](const auto& node)->K {
    using T=std::decay_t<decltype(node)>;
    if constexpr(std::is_same_v<T,TreeLiteral>)return PrimitiveKinds(node);
    else if constexpr(std::is_same_v<T,UnaryExpr>) {
      switch(node.operation) {
        case TreeUnary::kNot:return K::Boolean();
        case TreeUnary::kTypeof:return K::String();
        case TreeUnary::kPlus:return K::Number();
        case TreeUnary::kNegate:case TreeUnary::kBitNot: {
          const auto input=kinds[node.operand.value];
          return NumericValueKinds(input);
        }
      }
    } else if constexpr(std::is_same_v<T,BinaryExpr>) {
      switch(node.operation) {
        case analysis::BinaryOperator::kStrictEqual:case analysis::BinaryOperator::kStrictNotEqual:
        case analysis::BinaryOperator::kEqual:case analysis::BinaryOperator::kNotEqual:
        case analysis::BinaryOperator::kLess:case analysis::BinaryOperator::kLessEqual:
        case analysis::BinaryOperator::kGreater:case analysis::BinaryOperator::kGreaterEqual:
        case analysis::BinaryOperator::kIn:case analysis::BinaryOperator::kInstanceOf:return K::Boolean();
        case analysis::BinaryOperator::kShiftRightUnsigned:return K::Number();
        case analysis::BinaryOperator::kAdd: {
          const auto left=kinds[node.left.value],right=kinds[node.right.value];
          if(left.IsEmpty()||right.IsEmpty())return {};
          if(left.IsString()||right.IsString())return K::String();
          return CoercionPrimitive(left)&&CoercionPrimitive(right)?K::Number():K::Language();
        }
        case analysis::BinaryOperator::kSubtract:case analysis::BinaryOperator::kMultiply:
        case analysis::BinaryOperator::kDivide:case analysis::BinaryOperator::kRemainder:
        case analysis::BinaryOperator::kExponent:case analysis::BinaryOperator::kShiftLeft:
        case analysis::BinaryOperator::kShiftRightSigned:case analysis::BinaryOperator::kBitAnd:
        case analysis::BinaryOperator::kBitOr:case analysis::BinaryOperator::kBitXor: {
          const auto left=kinds[node.left.value],right=kinds[node.right.value];
          if(left.IsEmpty()||right.IsEmpty())return {};
          return CoercionPrimitive(left)&&CoercionPrimitive(right)?K::Number():K::Language();
        }
      }
    } else if constexpr(std::is_same_v<T,AssignmentExpr>)return kinds[node.value.value];
    else if constexpr(std::is_same_v<T,SequenceExpr>)return kinds[node.right.value];
    else if constexpr(std::is_same_v<T,ConditionalExpr>)return kinds[node.consequent.value].Join(kinds[node.alternate.value]);
    else if constexpr(std::is_same_v<T,LogicalExpr>)return kinds[node.left.value].Join(kinds[node.right.value]);
    else {
      static_assert(std::is_same_v<T,SymbolExpr>||std::is_same_v<T,GlobalNameExpr>||
          std::is_same_v<T,RealmObjectExpr>||std::is_same_v<T,SymbolConstructorExpr>||
          std::is_same_v<T,ThisExpr>||std::is_same_v<T,MemberExpr>||std::is_same_v<T,AwaitExpr>||
          std::is_same_v<T,YieldExpr>||std::is_same_v<T,UpdateExpr>||std::is_same_v<T,CallExpr>||
          std::is_same_v<T,ConstructExpr>||std::is_same_v<T,EmptyArrayExpr>||std::is_same_v<T,EmptyObjectExpr>||
          std::is_same_v<T,ObjectExpr>||std::is_same_v<T,FunctionExpr>||std::is_same_v<T,ArgumentSnapshotExpr>||
          std::is_same_v<T,ClassExpr>||std::is_same_v<T,FieldDefinitionExpr>||std::is_same_v<T,SuperCallExpr>);
      return K::Language();
    }
    throw TreeStructureError::kOwnership;
  },expression);
}
// ECMA-262 2025 GetValue/PutValue convert a retained reference's key, after
// receiver conversion (and after an assignment RHS). Acquisition is inert.
// Primitive conversion cannot invoke user hooks. Resource-observable contracts
// retain potential string allocation; absent precise domains stay conservative.
inline SourceEffects NormalPropertyKeyEffects(const MemberKey& key,
    std::span<const analysis::ValueKinds> kinds,ResourceFailureScope resources) {
  const auto* computed=std::get_if<ComputedKey>(&key);
  if(!computed)return {};
  const auto domain=kinds.empty()?analysis::ValueKinds::Language():kinds[computed->expression.value];
  if(domain.IsString()||(resources==ResourceFailureScope::kExcluded&&CoercionPrimitive(domain)))return {};
  if(CoercionPrimitive(domain))return {analysis::LanguageEffect::kAllocation,analysis::LanguageEffect::kAllocation,
      analysis::ExceptionalCompletion::kMayThrow};
  return CallbackEffects();
}
inline SourceEffects NormalPropertyReceiverEffects(ExprId base,
    std::span<const analysis::ValueKinds> kinds,ResourceFailureScope resources) {
  const auto domain=kinds.empty()?analysis::ValueKinds::Language():kinds[base.value];
  if(resources==ResourceFailureScope::kExcluded&&(domain.IsString()||domain.IsNumber()||domain.IsBoolean()))return {};
  // RequireObjectCoercible/ToObject may throw or allocate, but neither calls a
  // getter/proxy hook. Access and computed-key conversion have separate events.
  return {analysis::LanguageEffect::kAllocation,analysis::LanguageEffect::kGuard|analysis::LanguageEffect::kAllocation,
      analysis::ExceptionalCompletion::kMayThrow};
}
inline ExpressionEvaluation NormalPropertyKeyEvaluation(const MemberKey& key,
    std::span<const analysis::ValueKinds> kinds,ResourceFailureScope resources) {
  return NormalPropertyKeyEffects(key,kinds,resources).Evaluation();
}
inline ExpressionEvaluation NormalPropertyReceiverEvaluation(ExprId base,
    std::span<const analysis::ValueKinds> kinds,ResourceFailureScope resources) {
  return NormalPropertyReceiverEffects(base,kinds,resources).Evaluation();
}
// One exhaustive ECMA-262 effect table for canonical and projected source.
// Normal operand domains may exclude coercion hooks/language exceptions;
// GetValue, lexical initialization and child completion are separate facts.
// Undefined/Symbol/BigInt lack precise ValueKinds categories and remain unknown.
inline SourceEffects NormalValueEffects(const TreeExpression& expression,
    std::span<const analysis::ValueKinds> kinds,ResourceFailureScope resources,
    analysis::ValueKinds captured=analysis::ValueKinds::Language()) {
  using L=analysis::LanguageEffect;using X=analysis::ExceptionalCompletion;
  const SourceEffects guard{L::kGuard,L::kGuard,X::kMayThrow};
  const SourceEffects allocation{L::kAllocation,L::kAllocation,X::kMayThrow};
  return std::visit([&](const auto& node)->SourceEffects {
    using T=std::decay_t<decltype(node)>;
    if constexpr(std::is_same_v<T,TreeLiteral>||std::is_same_v<T,SequenceExpr>||
        std::is_same_v<T,LogicalExpr>||std::is_same_v<T,ConditionalExpr>)return {};
    else if constexpr(std::is_same_v<T,UnaryExpr>) {
      const bool primitive=!kinds.empty()&&CoercionPrimitive(kinds[node.operand.value]);
      switch(node.operation) {
        case TreeUnary::kNot:case TreeUnary::kTypeof:return {};
        case TreeUnary::kNegate:case TreeUnary::kBitNot:case TreeUnary::kPlus:
          return primitive?SourceEffects{}:CallbackEffects();
      }
    } else if constexpr(std::is_same_v<T,BinaryExpr>) {
      const auto left=kinds.empty()?analysis::ValueKinds::Language():kinds[node.left.value];
      const auto right=kinds.empty()?analysis::ValueKinds::Language():kinds[node.right.value];
      const bool primitive=CoercionPrimitive(left)&&CoercionPrimitive(right);
      switch(node.operation) {
        case analysis::BinaryOperator::kStrictEqual:case analysis::BinaryOperator::kStrictNotEqual:return {};
        case analysis::BinaryOperator::kAdd:
          if(!primitive)return CallbackEffects();
          if(resources==ResourceFailureScope::kObservable&&(left.IsString()||right.IsString()))return allocation;
          return {};
        case analysis::BinaryOperator::kSubtract:case analysis::BinaryOperator::kMultiply:
        case analysis::BinaryOperator::kDivide:case analysis::BinaryOperator::kRemainder:
        case analysis::BinaryOperator::kExponent:case analysis::BinaryOperator::kEqual:
        case analysis::BinaryOperator::kNotEqual:case analysis::BinaryOperator::kLess:
        case analysis::BinaryOperator::kLessEqual:case analysis::BinaryOperator::kGreater:
        case analysis::BinaryOperator::kGreaterEqual:case analysis::BinaryOperator::kShiftLeft:
        case analysis::BinaryOperator::kShiftRightSigned:case analysis::BinaryOperator::kShiftRightUnsigned:
        case analysis::BinaryOperator::kBitAnd:case analysis::BinaryOperator::kBitOr:
        case analysis::BinaryOperator::kBitXor:return primitive?SourceEffects{}:CallbackEffects();
        case analysis::BinaryOperator::kIn:case analysis::BinaryOperator::kInstanceOf:return CallbackEffects();
      }
    } else if constexpr(std::is_same_v<T,UpdateExpr>) {
      // ECMA-262 13.4: GetValue and success-only PutValue belong to separate
      // flow phases. Only this captured operand proves conversion inert.
      return CoercionPrimitive(captured)?SourceEffects{}:CallbackEffects();
    } else if constexpr(std::is_same_v<T,SymbolExpr>||std::is_same_v<T,ThisExpr>)return guard;
    else if constexpr(std::is_same_v<T,AssignmentExpr>) {
      // Declarative PutValue has no callback; its success-only write belongs
      // to a FlowDefinition. Member PutValue may invoke a setter/proxy trap.
      return std::holds_alternative<SymbolTarget>(node.target)?guard:CallbackEffects();
    } else if constexpr(std::is_same_v<T,EmptyArrayExpr>||std::is_same_v<T,EmptyObjectExpr>||std::is_same_v<T,FunctionExpr>)return allocation;
    else if constexpr(std::is_same_v<T,AwaitExpr>||std::is_same_v<T,YieldExpr>)return CallbackEffects(L::kSuspension);
    else if constexpr(std::is_same_v<T,GlobalNameExpr>||std::is_same_v<T,RealmObjectExpr>||std::is_same_v<T,SymbolConstructorExpr>)return CallbackEffects(L::kGlobalAccess);
    else {
      static_assert(std::is_same_v<T,MemberExpr>||
          std::is_same_v<T,CallExpr>||std::is_same_v<T,ConstructExpr>||
          std::is_same_v<T,ObjectExpr>||
          std::is_same_v<T,ArgumentSnapshotExpr>||std::is_same_v<T,ClassExpr>||
          std::is_same_v<T,FieldDefinitionExpr>||std::is_same_v<T,SuperCallExpr>);
    }
    return CallbackEffects();
  },expression);
}
inline ExpressionEvaluation NormalValueEvaluation(const TreeExpression& expression,
    std::span<const analysis::ValueKinds> kinds,ResourceFailureScope resources) {
  return NormalValueEffects(expression,kinds,resources).Evaluation();
}
inline ExpressionEvaluation IntrinsicEvaluation(const TreeExpression& expression) {
  return NormalValueEvaluation(expression,{},ResourceFailureScope::kObservable);
}
}
#endif
