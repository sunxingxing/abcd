#ifndef ABCD_ANALYSIS_REGEXP_FLAGS_H_
#define ABCD_ANALYSIS_REGEXP_FLAGS_H_
#include <array>
#include <cstdint>
namespace abcd::analysis {
// OH7 ets_runtime regexp/regexp_parser.h:40-47. These are runtime raw bits,
// NOT es2panda's lexer enum (whose sticky and dotAll positions differ).
struct RegExpFlags {
  std::uint8_t bits = 0;
  bool operator==(const RegExpFlags&) const = default;
};
struct RegExpFlag {
  std::uint8_t bit;
  char letter;
};
// BuiltinsRegExp::FlagsBitsToString, builtins_regexp.cpp:2376.
inline constexpr std::array<RegExpFlag, 7> kRegExpFlags{{{1U << 6U, 'd'},
                                                         {1U << 0U, 'g'},
                                                         {1U << 1U, 'i'},
                                                         {1U << 2U, 'm'},
                                                         {1U << 3U, 's'},
                                                         {1U << 4U, 'u'},
                                                         {1U << 5U, 'y'}}};
inline constexpr std::uint8_t kRegExpFlagMask = (1U << 7U) - 1;
inline constexpr std::uint8_t kRegExpUnicode = 1U << 4U;
}  // namespace abcd::analysis
#endif
