#include "abcd/structure/record_path.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <new>
#include <stdexcept>

namespace abcd::structure {
namespace {
enum class PathStyle { kArchive, kSource };
std::variant<RecordPath, PathError> EncodePath(
    const abc::RecordInfo& record, PathStyle style) {
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
  std::size_t position = 0;
  std::size_t component_end = name.find('/');
  if (component_end == std::string_view::npos) component_end = name.size();
  for (const unsigned char byte : name) {
    if (byte == '/') {
      if (component == 0) path.stem += "%00";
      path.stem += '/';
      component = 0;
      component_end = name.find('/', position + 1);
      if (component_end == std::string_view::npos) component_end = name.size();
    } else {
      const bool plain =
          (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
          (byte >= '0' && byte <= '9') || byte == '_' || byte == '-' ||
          (style == PathStyle::kSource && byte == '.' &&
           position + 1 < component_end);
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
    ++position;
  }
  if (component == 0) path.stem += "%00";
  if (path.stem.size() > kPathBytes) return PathError::kPathLimit;
  if (style == PathStyle::kSource) return path;
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
  if (path.stem.size() > kPathBytes) return PathError::kPathLimit;
  return path;
}
unsigned char Fold(unsigned char byte) {
  return byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte;
}
bool PathLess(const RecordPath* left, const RecordPath* right) {
  return std::lexicographical_compare(
      left->stem.begin(), left->stem.end(),
      right->stem.begin(), right->stem.end(),
      [](unsigned char a, unsigned char b) { return Fold(a) < Fold(b); });
}
}  // namespace

std::variant<RecordPath, PathError> PlanRecordPath(
    const abc::RecordInfo& record) {
  return EncodePath(record, PathStyle::kArchive);
}

std::variant<std::vector<RecordPath>, PathError> PlanRecordSourcePaths(
    std::span<const abc::RecordInfo> records, std::size_t budget_bytes) {
  constexpr auto kRecordBytes = sizeof(RecordPath) + sizeof(const RecordPath*);
  if (records.size() > budget_bytes / kRecordBytes)
    return PathError::kResourceLimit;
  try {
    std::vector<RecordPath> paths;
    std::vector<const RecordPath*> order;
    paths.reserve(records.size());
    order.reserve(records.size());
    if (paths.capacity() > budget_bytes / sizeof(RecordPath))
      return PathError::kResourceLimit;
    budget_bytes -= paths.capacity() * sizeof(RecordPath);
    if (order.capacity() > budget_bytes / sizeof(const RecordPath*))
      return PathError::kResourceLimit;
    budget_bytes -= order.capacity() * sizeof(const RecordPath*);
    for (const auto& record : records) {
      auto planned = EncodePath(record, PathStyle::kSource);
      if (const auto* error = std::get_if<PathError>(&planned)) return *error;
      auto path = std::get<RecordPath>(std::move(planned));
      if (path.stem.capacity() >= budget_bytes) return PathError::kResourceLimit;
      budget_bytes -= path.stem.capacity() + 1;
      paths.push_back(std::move(path));
      order.push_back(&paths.back());
    }
    // ASCII folding avoids silent overwrite on case-insensitive export hosts.
    // Percent escapes are injective; literal '%' is always escaped too.
    std::sort(order.begin(), order.end(), PathLess);
    for (std::size_t i = 1; i < order.size(); ++i)
      if (!PathLess(order[i - 1], order[i])) return PathError::kCollision;
    return paths;
  } catch (const std::bad_alloc&) {
    return PathError::kResourceLimit;
  } catch (const std::length_error&) {
    return PathError::kResourceLimit;
  }
}
}  // namespace abcd::structure
