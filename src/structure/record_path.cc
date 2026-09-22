#include "abcd/structure/record_path.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace abcd::structure {
std::variant<RecordPath, PathError> PlanRecordPath(
    const abc::RecordInfo& record) {
  constexpr std::size_t kComponentBytes = 200;
  constexpr std::size_t kPathBytes = 2048;
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string_view name = record.descriptor;
  if (name.size() < 3 || name.front() != 'L' || name.back() != ';')
    return PathError::kDescriptor;
  name.remove_prefix(1);
  name.remove_suffix(1);
  RecordPath path;
  std::size_t component = 0;
  for (const unsigned char byte : name) {
    if (byte == '/') {
      if (component == 0) path.stem += "%00";
      path.stem += '/';
      component = 0;
    } else {
      const bool plain =
          (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
          (byte >= '0' && byte <= '9') || byte == '_' || byte == '-';
      const auto count = plain ? 1u : 3u;
      if (count > kComponentBytes - component)
        return PathError::kComponentLimit;
      if (plain)
        path.stem += static_cast<char>(byte);
      else {
        path.stem += '%';
        path.stem += kHex[byte >> 4];
        path.stem += kHex[byte & 0xf];
      }
      component += count;
    }
    if (path.stem.size() > kPathBytes) return PathError::kPathLimit;
  }
  if (component == 0) path.stem += "%00";
  constexpr std::size_t kEntityDigits = sizeof(record.offset.value) * 2;
  std::array<char, kEntityDigits> digits;
  digits.fill('0');
  std::array<char, kEntityDigits> encoded{};
  const auto end =
      std::to_chars(encoded.data(), encoded.data() + encoded.size(),
                    record.offset.value, 16)
          .ptr;
  const auto size = static_cast<std::size_t>(end - encoded.data());
  std::copy(encoded.begin(), encoded.begin() + size, digits.end() - size);
  path.stem += "~r";
  path.stem.append(digits.data(), digits.size());
  return path;
}
}  // namespace abcd::structure
