#ifndef ABCD_SOURCE_LEXICAL_NAME_H_
#define ABCD_SOURCE_LEXICAL_NAME_H_

#include <array>
#include <charconv>
#include <limits>
#include <optional>
#include <string_view>

#include "abcd/abc/metadata_index.h"
#include "abcd/analysis/linear_lexicals.h"

namespace abcd::source {
// Same private parent-cell ABI for legacy closure emission and generic
// template binding. This name does not establish source-variable identity.
class LexicalName {
 public:
  explicit LexicalName(analysis::LocalCell cell)
      : LexicalName(cell, std::nullopt) {}
  LexicalName(analysis::LocalCell cell, abc::EntityOffset method)
      : LexicalName(cell, std::optional{method}) {}
  std::string_view Text() const { return {bytes_.data(), size_}; }

 private:
  LexicalName(analysis::LocalCell cell, std::optional<abc::EntityOffset> method) {
    auto* next = bytes_.data();
    const auto prefix = method ? kInnerPrefix : kPrefix;
    for (const char ch : prefix) *next++ = ch;
    // One-file, non-recursive child expansion: distinguish template locals
    // from locals in the enclosing template, including equal env/slot IDs.
    if (method) {
      next = std::to_chars(next, bytes_.data() + bytes_.size(), method->value).ptr;
      *next++ = '_';
    }
    next = std::to_chars(next, bytes_.data() + bytes_.size(),
                         cell.environment.value).ptr;
    *next++ = '_';
    next = std::to_chars(next, bytes_.data() + bytes_.size(),
                         cell.slot.value).ptr;
    size_ = static_cast<std::size_t>(next - bytes_.data());
  }
  static constexpr std::string_view kPrefix = "$lex";
  static constexpr std::string_view kInnerPrefix = "$inner";
  static constexpr auto kEnvironmentDigits =
      std::numeric_limits<decltype(analysis::EnvironmentId{}.value)>::digits10 + 1;
  static constexpr auto kSlotDigits =
      std::numeric_limits<decltype(analysis::LexicalSlot{}.value)>::digits10 + 1;
  static constexpr auto kMethodDigits =
      std::numeric_limits<decltype(abc::EntityOffset{}.value)>::digits10 + 1;
  std::array<char, kInnerPrefix.size() + kMethodDigits + 1 +
                       kEnvironmentDigits + 1 + kSlotDigits> bytes_{};
  std::size_t size_ = 0;
};
}  // namespace abcd::source
#endif  // ABCD_SOURCE_LEXICAL_NAME_H_
