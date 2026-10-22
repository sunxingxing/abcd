#include "template_literal.h"
namespace abcd::source {
namespace {
constexpr auto kSyntax = TreeStructureError::kUnsupportedSyntax;
int Hex(char16_t c) {
  return c >= u'0' && c <= u'9'   ? c - u'0'
         : c >= u'a' && c <= u'f' ? c - u'a' + 10
         : c >= u'A' && c <= u'F' ? c - u'A' + 10
                                  : -1;
}
bool Digit(char16_t c) { return c >= u'0' && c <= u'9'; }
}  // namespace
std::optional<std::u16string> CookTemplateRaw(std::u16string_view raw,
                                              TreeCheckBudget& b) {
  b.Admit(raw.size() + 1, sizeof(char16_t));
  std::u16string cooked;
  cooked.reserve(raw.size());
  b.Admit(cooked.capacity() > raw.size() ? cooked.capacity() - raw.size() : 0,
          sizeof(char16_t));
  bool valid = true;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    b.Step();
    const auto c = raw[i];
    RequireTree(c != u'`' && c != u'\r' &&
                    !(c == u'$' && i + 1 < raw.size() && raw[i + 1] == u'{'),
                kSyntax);
    // Literal surrogate pairs are UTF-8 representable. Isolated units cannot
    // be source-escaped without changing the raw text.
    if (c >= 0xd800 && c <= 0xdbff) {
      RequireTree(
          i + 1 < raw.size() && raw[i + 1] >= 0xdc00 && raw[i + 1] <= 0xdfff,
          kSyntax);
      cooked += c;
      cooked += raw[++i];
      continue;
    }
    RequireTree(c < 0xdc00 || c > 0xdfff, kSyntax);
    if (c != u'\\') {
      cooked += c;
      continue;
    }
    RequireTree(++i < raw.size(), kSyntax);
    b.Step();
    const auto e = raw[i];
    switch (e) {
      case u'\n':
      case 0x2028:
      case 0x2029:
        break;
      case u'\r':
        throw kSyntax;
      case u'b':
        cooked += u'\b';
        break;
      case u'f':
        cooked += u'\f';
        break;
      case u'n':
        cooked += u'\n';
        break;
      case u'r':
        cooked += u'\r';
        break;
      case u't':
        cooked += u'\t';
        break;
      case u'v':
        cooked += u'\v';
        break;
      case u'0':
        if (i + 1 < raw.size() && Digit(raw[i + 1]))
          valid = false;
        else
          cooked += char16_t{};
        break;
      case u'x':
      case u'u': {
        std::uint32_t point = 0;
        unsigned count = e == u'x' ? 2 : 4;
        bool braces = e == u'u' && i + 1 < raw.size() && raw[i + 1] == u'{';
        if (braces) {
          ++i;
          count = 0;
        }
        bool good = true;
        unsigned n = 0;
        while (i + 1 < raw.size() &&
               (braces ? raw[i + 1] != u'}' : n < count)) {
          b.Step();
          const auto h = Hex(raw[i + 1]);
          if (h < 0) {
            good = false;
            break;
          }
          ++i;
          ++n;
          if (n > 6) {
            good = false;
            break;
          }
          point = (point << 4) | unsigned(h);
        }
        if (braces) {
          if (!n || i + 1 == raw.size() || raw[i + 1] != u'}')
            good = false;
          else
            ++i;
        } else if (n != count)
          good = false;
        if (point > 0x10ffff) good = false;
        if (!good) {
          valid = false;
          break;
        }
        if (point < 0x10000)
          cooked += static_cast<char16_t>(point);
        else {
          point -= 0x10000;
          cooked += static_cast<char16_t>(0xd800 + (point >> 10));
          cooked += static_cast<char16_t>(0xdc00 + (point & 1023));
        }
        break;
      }
      default:
        if (Digit(e))
          valid = false;
        else
          cooked += e;
        break;
    }
  }
  return valid ? std::optional{std::move(cooked)} : std::nullopt;
}
void VerifyTemplateLiteral(const TemplateObjectExpr& node,
                           std::span<const std::u16string> texts,
                           SourceContract contract, TreeCheckBudget& b) {
  RequireTree(TaggedTemplates(contract) && node.quasis && !node.quasis->empty(),
              kSyntax);
  for (const auto& q : *node.quasis) {
    b.Step();
    RequireTree(q.raw.value < texts.size() &&
                    (!q.cooked || q.cooked->value < texts.size()),
                kSyntax);
    const auto actual = CookTemplateRaw(texts[q.raw.value], b);
    RequireTree(actual.has_value() == q.cooked.has_value(), kSyntax);
    if (actual) {
      b.Step(actual->size());
      RequireTree(*actual == texts[q.cooked->value], kSyntax);
    }
  }
}
}  // namespace abcd::source
