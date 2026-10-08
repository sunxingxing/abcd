#ifndef ABCD_SOURCE_PRIMITIVE_VALUE_H_
#define ABCD_SOURCE_PRIMITIVE_VALUE_H_
#include "abcd/source/source_tree.h"
#include <bit>
#include <cmath>
#include <span>
#include <type_traits>
namespace abcd::source {
// Identity in one immutable source primitive pool, independent of expression
// ownership. Implicit initialization values need no original literal node.
struct PrimitiveId {std::uint32_t value=0;auto operator<=>(const PrimitiveId&)const=default;};
inline bool PrimitiveTruth(const TreeLiteral& literal,std::span<const std::u16string> texts) {
  return std::visit([&](const auto& value) {
    using T=std::decay_t<decltype(value)>;
    if constexpr(std::is_same_v<T,UndefinedLiteral>||std::is_same_v<T,NullLiteral>)return false;
    else if constexpr(std::is_same_v<T,BooleanLiteral>||std::is_same_v<T,IntegerLiteral>)return bool(value.value);
    else if constexpr(std::is_same_v<T,NumberLiteral>) {const auto number=std::bit_cast<double>(value.bits);return number!=0&&!std::isnan(number);}
    else {static_assert(std::is_same_v<T,TextId>);return !texts[value.value].empty();}
  },literal);
}
inline bool PrimitiveNullish(const TreeLiteral& literal) {
  return std::holds_alternative<UndefinedLiteral>(literal)||std::holds_alternative<NullLiteral>(literal);
}
// Semantic adapter canonicalizes UTF-16 text identities within one input.
// Preserve numeric representation/bits; this is a value join, not JS equality.
inline bool SamePrimitiveValue(const TreeLiteral& a,const TreeLiteral& b) {
  return std::visit([&](const auto& value) {
    using T=std::decay_t<decltype(value)>;const auto* other=std::get_if<T>(&b);
    if(!other)return false;
    if constexpr(std::is_same_v<T,UndefinedLiteral>||std::is_same_v<T,NullLiteral>)return true;
    else if constexpr(std::is_same_v<T,BooleanLiteral>||std::is_same_v<T,IntegerLiteral>)return value.value==other->value;
    else if constexpr(std::is_same_v<T,NumberLiteral>)return value.bits==other->bits;
    else {static_assert(std::is_same_v<T,TextId>);return value==*other;}
  },a);
}
}
#endif
