#ifndef ABCD_SOURCE_MEMBER_KEY_H_
#define ABCD_SOURCE_MEMBER_KEY_H_

#include <algorithm>
#include <string_view>

namespace abcd::source {
enum class MemberKeySyntax { kIdentifier, kQuoted };
// MemberExpression uses IdentifierName, not BindingIdentifier: keywords are
// legal after '.'. This must not select class/object declaration syntax
// (constructor/__proto__ have different semantics there).
inline MemberKeySyntax ClassifyMemberKey(std::u16string_view name) {
  const auto start = [](char16_t ch) {
    return (ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z') ||
           ch == u'_' || ch == u'$';
  };
  if (name.empty() || !start(name.front())) return MemberKeySyntax::kQuoted;
  return std::all_of(name.begin() + 1, name.end(), [&](char16_t ch) {
    return start(ch) || (ch >= u'0' && ch <= u'9');
  }) ? MemberKeySyntax::kIdentifier : MemberKeySyntax::kQuoted;
}
// Class MethodDefinition only, not fields or object data properties. Literal
// constructor/prototype keys have grammar-specific restrictions; keep their
// computed spelling irrespective of placement. This does not admit a member
// rejected by the independent class descriptor/ownership proof.
inline MemberKeySyntax ClassifyMethodKey(std::u16string_view name) {
  if (name == u"constructor" || name == u"prototype")
    return MemberKeySyntax::kQuoted;
  return ClassifyMemberKey(name);
}
}  // namespace abcd::source
#endif
