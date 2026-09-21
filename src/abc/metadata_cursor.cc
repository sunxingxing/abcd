#include "metadata_cursor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>

#include "abcd/corpus/abc_file.h"

namespace abcd::abc {
namespace {

// Pinned runtime_core 3e147bb: libpandabase/utils/leb128.h and
// libpandafile/file_items.cpp StringItem::Write.
constexpr std::size_t kMaxLebBytes = 5;
constexpr std::uint8_t kPayloadMask = 0x7f;
constexpr std::uint8_t kContinuation = 0x80;
constexpr std::size_t kLebBits = 7;
constexpr std::uint32_t kMaxStringBytes = 16 * 1024 * 1024;

}  // namespace

MetadataCursor::MetadataCursor(const corpus::AbcFile& file,
                               std::uint32_t offset, std::uint32_t limit)
    : file_(file), position_(offset), limit_(limit) {}

std::uint32_t MetadataCursor::Position() const { return position_; }

std::variant<std::uint8_t, MetadataError> MetadataCursor::U8() {
  if (position_ >= limit_) return MetadataError::kTruncated;
  if (position_ < buffer_start_ || position_ >= buffer_end_) {
    buffer_start_ = position_;
    const auto length = static_cast<std::size_t>(
        std::min<std::uint32_t>(kWindowBytes, limit_ - position_));
    if (file_.ReadAt(position_, std::span(buffer_).first(length)))
      return MetadataError::kRead;
    buffer_end_ = position_ + static_cast<std::uint32_t>(length);
  }
  return buffer_[position_++ - buffer_start_];
}

std::variant<std::uint16_t, MetadataError> MetadataCursor::U16() {
  const auto low = U8();
  const auto high = U8();
  if (std::holds_alternative<MetadataError>(low))
    return std::get<MetadataError>(low);
  if (std::holds_alternative<MetadataError>(high))
    return std::get<MetadataError>(high);
  return static_cast<std::uint16_t>(
      std::get<std::uint8_t>(low) | (std::get<std::uint8_t>(high) << 8));
}

std::variant<std::uint32_t, MetadataError> MetadataCursor::U32() {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto byte = U8();
    if (std::holds_alternative<MetadataError>(byte))
      return std::get<MetadataError>(byte);
    value |= static_cast<std::uint32_t>(std::get<std::uint8_t>(byte))
             << (i * 8);
  }
  return value;
}

std::variant<std::uint32_t, MetadataError> MetadataCursor::Uleb() {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < kMaxLebBytes; ++i) {
    const auto byte = U8();
    if (std::holds_alternative<MetadataError>(byte))
      return std::get<MetadataError>(byte);
    const auto raw = std::get<std::uint8_t>(byte);
    if (i == kMaxLebBytes - 1 &&
        ((raw & kContinuation) || (raw & kPayloadMask) > 15))
      return MetadataError::kMalformedLeb;
    value |= static_cast<std::uint32_t>(raw & kPayloadMask)
             << (i * kLebBits);
    if (!(raw & kContinuation)) return value;
  }
  return MetadataError::kMalformedLeb;
}

std::optional<MetadataError> MetadataCursor::SkipSleb() {
  for (std::size_t i = 0; i < kMaxLebBytes; ++i) {
    const auto byte = U8();
    if (std::holds_alternative<MetadataError>(byte))
      return std::get<MetadataError>(byte);
    const auto raw = std::get<std::uint8_t>(byte);
    if (i == kMaxLebBytes - 1) {
      const auto high = raw & 0x70;
      const auto sign = raw & 0x08;
      if ((raw & kContinuation) || (high != (sign ? 0x70 : 0)))
        return MetadataError::kMalformedLeb;
    }
    if (!(raw & kContinuation)) return std::nullopt;
  }
  return MetadataError::kMalformedLeb;
}

bool MetadataCursor::Skip(std::uint64_t count) {
  if (count > limit_ - position_) return false;
  position_ += static_cast<std::uint32_t>(count);
  return true;
}

std::variant<std::string, MetadataError> ReadMetadataString(
    MetadataCursor* cursor, std::uint32_t* utf16_length,
    std::size_t* spent, std::size_t budget) {
  const auto prefix = cursor->Uleb();
  if (std::holds_alternative<MetadataError>(prefix))
    return std::get<MetadataError>(prefix);
  const auto encoded = std::get<std::uint32_t>(prefix);
  *utf16_length = encoded >> 1;
  const bool ascii = (encoded & 1) != 0;
  std::string value;
  std::uint32_t units = 0;
  for (std::uint32_t i = 0; i <= kMaxStringBytes; ++i) {
    const auto byte = cursor->U8();
    if (std::holds_alternative<MetadataError>(byte))
      return std::get<MetadataError>(byte);
    const auto raw = std::get<std::uint8_t>(byte);
    if (raw == 0) {
      if (units != *utf16_length) return MetadataError::kBadString;
      return value;
    }
    if (ascii && raw > 0x7f) return MetadataError::kBadString;
    if (*spent >= budget) return MetadataError::kResourceLimit;
    value.push_back(static_cast<char>(raw));
    ++*spent;
    if (raw >= 0x80) {
      if (raw < 0xc0 || raw >= 0xf0) return MetadataError::kBadString;
      const std::size_t followers = raw < 0xe0 ? 1 : 2;
      std::uint32_t decoded = raw & (followers == 1 ? 0x1f : 0x0f);
      for (std::size_t part = 0; part < followers; ++part) {
        const auto next = cursor->U8();
        if (std::holds_alternative<MetadataError>(next))
          return std::get<MetadataError>(next);
        const auto continuation = std::get<std::uint8_t>(next);
        if ((continuation & 0xc0) != 0x80)
          return MetadataError::kBadString;
        decoded = (decoded << 6) | (continuation & 0x3f);
        if (*spent >= budget) return MetadataError::kResourceLimit;
        value.push_back(static_cast<char>(continuation));
        ++*spent;
      }
      if ((followers == 1 && decoded < 0x80 && decoded != 0) ||
          (followers == 2 && decoded < 0x800))
        return MetadataError::kBadString;
    }
    ++units;
  }
  return MetadataError::kResourceLimit;
}

}  // namespace abcd::abc
