#ifndef ABCD_SOURCE_BIGINT_LITERAL_H_
#define ABCD_SOURCE_BIGINT_LITERAL_H_
#include <string_view>
namespace abcd::source {
// ECMA-262 BigIntLiteral's unsigned digits. LDBIGINT may hold arbitrary text;
// never print a runtime SyntaxError as an early source parse error. Signs and
// whitespace are not literal tokens. Prefix forms preserve their exact value.
inline bool ValidBigIntLiteral(std::u16string_view text) {
  if (text.empty()) return false;
  unsigned base = 10;
  std::size_t first = 0;
  if (text.size() > 1 && text[0] == u'0') {
    switch (text[1]) {
      case u'x': case u'X': base = 16; break;
      case u'o': case u'O': base = 8; break;
      case u'b': case u'B': base = 2; break;
      default: return false;
    }
    first = 2;
  }
  if (first == text.size()) return false;
  for (; first < text.size(); ++first) {
    const auto c = text[first];
    const unsigned digit = c >= u'0' && c <= u'9' ? c - u'0' :
        c >= u'a' && c <= u'f' ? c - u'a' + 10 :
        c >= u'A' && c <= u'F' ? c - u'A' + 10 : base;
    if (digit >= base) return false;
  }
  return true;
}
}
#endif
