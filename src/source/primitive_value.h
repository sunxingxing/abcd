#ifndef ABCD_SOURCE_PRIMITIVE_VALUE_H_
#define ABCD_SOURCE_PRIMITIVE_VALUE_H_
#include "abcd/source/source_tree.h"
#include <type_traits>
namespace abcd::source {
// Identity in one immutable source primitive pool, independent of expression
// ownership. Implicit initialization values need no original literal node.
struct PrimitiveId {std::uint32_t value=0;auto operator<=>(const PrimitiveId&)const=default;};
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
