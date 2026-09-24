#ifndef ABCD_SOURCE_ELEMENT_NAME_H_
#define ABCD_SOURCE_ELEMENT_NAME_H_

#include <cstdint>
#include <limits>
#include <string_view>

namespace abcd::source {
inline bool IsElementName(std::u16string_view name) {
  constexpr auto kIndexDigits =
      std::numeric_limits<std::uint32_t>::digits10 + 1;
  constexpr std::uint64_t kMaxElementIndex = UINT32_MAX - 1;
  constexpr std::uint64_t kDecimalRadix = 10;
  if (name.empty() || name.size() > kIndexDigits ||
      (name.size() > 1 && name.front() == u'0'))
    return false;
  std::uint64_t value = 0;
  for (const auto digit : name) {
    if (digit < u'0' || digit > u'9') return false;
    value = value * kDecimalRadix + (digit - u'0');
  }
  return value <= kMaxElementIndex;
}
}  // namespace abcd::source

#endif  // ABCD_SOURCE_ELEMENT_NAME_H_
