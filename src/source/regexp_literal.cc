#include "regexp_literal.h"

#include <limits>
#include <vector>
namespace abcd::source {
namespace {
using Error = RegExpLiteralError;
struct Group {
  bool assertion = false;
};
struct Atom {
  std::uint32_t point;
  bool character = true;
  bool assertion = false;
};
class Pattern {
 public:
  Pattern(std::u16string_view text, analysis::RegExpFlags flags,
          analysis::SemanticBudget budget)
      : text_(text),
        unicode_((flags.bits & analysis::kRegExpUnicode) != 0),
        left_(budget),
        initial_(budget) {}
  RegExpLiteralPlan Run(analysis::RegExpFlags flags) {
    if (flags.bits & ~analysis::kRegExpFlagMask) throw Error::kFlags;
    if (text_.size() > (std::numeric_limits<std::size_t>::max() - 16) / 4)
      throw Error::kResourceLimit;
    Charge(text_.size() + 1, sizeof(Group));
    stack_.reserve(text_.size() + 1);
    Charge(stack_.capacity() - (text_.size() + 1), sizeof(Group));
    bool atom = false, quantified = false;
    while (at_ < text_.size()) {
      Step();
      const auto c = text_[at_++];
      switch (c) {
        case u'(': {
          bool assertion = false;
          if (Take(u'?')) {
            if (Take(u':')) {
            } else if (Take(u'=') || Take(u'!'))
              assertion = true;
            else if (Take(u'<')) {
              if (!Take(u'=') && !Take(u'!')) throw Error::kUnproven;
              assertion = true;
            } else
              throw Error::kUnproven;
          }
          stack_.push_back({assertion});
          atom = false;
          quantified = false;
          break;
        }
        case u')':
          if (stack_.empty()) throw Error::kSyntax;
          atom = !stack_.back().assertion;
          stack_.pop_back();
          quantified = false;
          break;
        case u'|':
          atom = false;
          quantified = false;
          break;
        case u'^':
        case u'$':
          atom = false;
          quantified = false;
          break;
        case u'[':
          Class();
          atom = true;
          quantified = false;
          break;
        case u'\\': {
          auto escaped = Escape(false);
          atom = !escaped.assertion;
          quantified = false;
          break;
        }
        case u'*':
        case u'+':
        case u'?':
          if (!atom || quantified) throw Error::kSyntax;
          Take(u'?');
          quantified = true;
          break;
        case u'{':
          if (!atom || quantified) throw Error::kUnproven;
          Quantifier();
          Take(u'?');
          quantified = true;
          break;
        case u']':
        case u'}':
          throw Error::kUnproven;  // Annex B legacy literals need a separate
                                   // proof.
        default:
          Literal(c);
          atom = true;
          quantified = false;
          break;
      }
    }
    if (!stack_.empty()) throw Error::kSyntax;
    if (text_.size() > (std::numeric_limits<std::size_t>::max() - 16) / 4)
      throw Error::kResourceLimit;
    const auto capacity = 4 * text_.size() + 16;
    Charge(capacity + 1, 1);
    RegExpLiteralPlan result;
    result.literal.reserve(capacity);
    Charge(result.literal.capacity() - capacity, 1);
    result.literal.push_back('/');
    if (text_.empty()) result.literal += "(?:)";
    // Match OH7 EscapeRegExpPattern: escape every unescaped slash, including
    // in a class, so the observable .source spelling is preserved too.
    bool escaped = false;
    for (std::size_t i = 0; i < text_.size(); ++i) {
      Step();
      const auto c = text_[i];
      if (c == u'/' && !escaped) result.literal.push_back('\\');
      if (c == u'\\')
        escaped = !escaped;
      else
        escaped = false;
      std::uint32_t point = c;
      if (c >= 0xd800 && c <= 0xdbff) {
        if (++i >= text_.size() || text_[i] < 0xdc00 || text_[i] > 0xdfff)
          throw Error::kSourceRepresentation;
        point = 0x10000 + ((c - 0xd800) << 10) + (text_[i] - 0xdc00);
      } else if (c >= 0xdc00 && c <= 0xdfff)
        throw Error::kSourceRepresentation;
      Utf8(result.literal, point);
    }
    result.literal.push_back('/');
    for (const auto f : analysis::kRegExpFlags) {
      Step();
      if (flags.bits & f.bit) result.literal.push_back(f.letter);
    }
    result.allocated_bytes = initial_.bytes - left_.bytes;
    result.work = initial_.work - left_.work;
    return result;
  }

 private:
  void Charge(std::size_t count, std::size_t width) {
    if (count > left_.bytes / width) throw Error::kResourceLimit;
    left_.bytes -= count * width;
  }
  void Step() {
    if (!left_.work) throw Error::kWorkLimit;
    --left_.work;
  }
  bool Take(char16_t c) {
    Step();
    if (at_ < text_.size() && text_[at_] == c) {
      ++at_;
      return true;
    }
    return false;
  }
  static bool Digit(char16_t c) { return c >= u'0' && c <= u'9'; }
  static int Hex(char16_t c) {
    return c >= u'0' && c <= u'9'   ? c - u'0'
           : c >= u'a' && c <= u'f' ? c - u'a' + 10
           : c >= u'A' && c <= u'F' ? c - u'A' + 10
                                    : -1;
  }
  static bool Syntax(char16_t c) {
    switch (c) {
      case u'^':
      case u'$':
      case u'\\':
      case u'.':
      case u'*':
      case u'+':
      case u'?':
      case u'(':
      case u')':
      case u'[':
      case u']':
      case u'{':
      case u'}':
      case u'|':
      case u'/':
        return true;
      default:
        return false;
    }
  }
  void Literal(char16_t c) {
    // The pinned .source getter escapes slashes but NOT actual line
    // terminators. Escaping those for native syntax would change this
    // observable property.
    if (c == u'\n' || c == u'\r' || c == 0x2028 || c == 0x2029)
      throw Error::kSourceRepresentation;
  }
  std::uint32_t HexDigits(unsigned count) {
    std::uint32_t n = 0;
    for (unsigned i = 0; i < count; ++i) {
      Step();
      if (at_ == text_.size()) throw Error::kSyntax;
      int x = Hex(text_[at_++]);
      if (x < 0) throw Error::kUnproven;
      n = (n << 4) | unsigned(x);
    }
    return n;
  }
  Atom Escape(bool in_class) {
    Step();
    if (at_ == text_.size()) throw Error::kSyntax;
    const auto c = text_[at_++];
    Literal(c);
    switch (c) {
      case u'd':
      case u'D':
      case u's':
      case u'S':
      case u'w':
      case u'W':
        return {0, false};
      case u'b':
        return {8, in_class, !in_class};
      case u'B':
        if (in_class) throw Error::kUnproven;
        return {0, false, true};
      case u'f':
        return {12};
      case u'n':
        return {10};
      case u'r':
        return {13};
      case u't':
        return {9};
      case u'v':
        return {11};
      case u'0':
        if (at_ < text_.size() && Digit(text_[at_])) throw Error::kUnproven;
        return {0};
      case u'x':
        return {HexDigits(2)};
      case u'u': {
        if (at_ < text_.size() && text_[at_] == u'{') {
          if (!unicode_) throw Error::kUnproven;
          ++at_;
          std::uint32_t n = 0;
          unsigned digits = 0;
          while (at_ < text_.size() && text_[at_] != u'}') {
            Step();
            auto x = Hex(text_[at_++]);
            if (x < 0 || ++digits > 6) throw Error::kSyntax;
            n = (n << 4) | unsigned(x);
          }
          if (!digits || n > 0x10ffff || !Take(u'}')) throw Error::kSyntax;
          return {n};
        }
        const auto n = HexDigits(4);
        if (unicode_ && n >= 0xd800 && n <= 0xdfff) throw Error::kUnproven;
        return {n};
      }
      case u'c': {
        Step();
        if (at_ == text_.size()) throw Error::kUnproven;
        const auto x = text_[at_++];
        if (!((x >= u'A' && x <= u'Z') || (x >= u'a' && x <= u'z')))
          throw Error::kUnproven;
        return {unsigned(x) % 32};
      }
      case u'p':
      case u'P':
      case u'k':
        throw Error::kUnproven;
      default:
        if (Digit(c)) throw Error::kUnproven;
        if (unicode_ && !Syntax(c) && !(in_class && c == u'-'))
          throw Error::kSyntax;
        return {c};
    }
  }
  Atom ClassAtom() {
    Step();
    if (at_ == text_.size()) throw Error::kSyntax;
    const auto c = text_[at_++];
    if (c == u'\\') return Escape(true);
    Literal(c);
    if (c >= 0xd800 && c <= 0xdfff) throw Error::kUnproven;
    return {c};
  }
  void Class() {
    Take(u'^');
    while (at_ < text_.size() && text_[at_] != u']') {
      auto from = ClassAtom();
      if (at_ < text_.size() && text_[at_] == u'-' && at_ + 1 < text_.size() &&
          text_[at_ + 1] != u']') {
        ++at_;
        auto to = ClassAtom();
        if (!from.character || !to.character) throw Error::kUnproven;
        if (from.point > to.point) throw Error::kSyntax;
      }
    }
    if (!Take(u']')) throw Error::kSyntax;
  }
  std::uint32_t Decimal() {
    if (at_ == text_.size() || !Digit(text_[at_])) throw Error::kUnproven;
    std::uint32_t n = 0;
    while (at_ < text_.size() && Digit(text_[at_])) {
      Step();
      auto d = unsigned(text_[at_++] - u'0');
      if (n > (INT32_MAX - d) / 10) throw Error::kUnproven;
      n = 10 * n + d;
    }
    return n;
  }
  void Quantifier() {
    auto low = Decimal();
    if (Take(u',')) {
      if (at_ < text_.size() && text_[at_] != u'}') {
        auto high = Decimal();
        if (high < low) throw Error::kSyntax;
      }
    }
    if (!Take(u'}')) throw Error::kUnproven;
  }
  static void Utf8(std::string& out, std::uint32_t p) {
    if (p < 0x80)
      out.push_back(char(p));
    else if (p < 0x800) {
      out.push_back(char(0xc0 | (p >> 6)));
      out.push_back(char(0x80 | (p & 63)));
    } else if (p < 0x10000) {
      out.push_back(char(0xe0 | (p >> 12)));
      out.push_back(char(0x80 | ((p >> 6) & 63)));
      out.push_back(char(0x80 | (p & 63)));
    } else {
      out.push_back(char(0xf0 | (p >> 18)));
      out.push_back(char(0x80 | ((p >> 12) & 63)));
      out.push_back(char(0x80 | ((p >> 6) & 63)));
      out.push_back(char(0x80 | (p & 63)));
    }
  }
  std::u16string_view text_;
  bool unicode_;
  analysis::SemanticBudget left_, initial_;
  std::size_t at_ = 0;
  std::vector<Group> stack_;
};
}  // namespace
std::variant<RegExpLiteralPlan, RegExpLiteralError> BuildRegExpLiteral(
    std::u16string_view text, analysis::RegExpFlags flags,
    analysis::SemanticBudget budget) {
  try {
    return Pattern(text, flags, budget).Run(flags);
  } catch (Error e) {
    return e;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
bool VerifyRegExpLiteral(std::u16string_view text, analysis::RegExpFlags flags,
                         const RegExpLiteralPlan& supplied,
                         analysis::SemanticBudget budget) {
  auto built = BuildRegExpLiteral(text, flags, budget);
  const auto* proof = std::get_if<RegExpLiteralPlan>(&built);
  return proof && proof->literal == supplied.literal;
}
}  // namespace abcd::source
