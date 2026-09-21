#include "metadata_cursor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
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
constexpr std::uint8_t kFinalUlebPayloadMax = 0x0f;
constexpr std::uint8_t kSlebHighMask = 0x70;
constexpr std::uint8_t kSlebSignBit = 0x08;
constexpr std::size_t kBitsPerByte = 8;
constexpr std::uint32_t kMaxStringBytes = 16 * 1024 * 1024;
constexpr std::uint8_t kStringAsciiFlag = 1;
constexpr std::size_t kStringLengthShift = 1;
constexpr std::uint8_t kAsciiMax = 0x7f;
constexpr std::uint8_t kTwoByteLeadMin = 0xc0;
constexpr std::uint8_t kThreeByteLeadMin = 0xe0;
constexpr std::uint8_t kFourByteLeadMin = 0xf0;
constexpr std::uint8_t kFourByteLeadMax = 0xf4;
constexpr std::uint8_t kTwoByteLeadMask = 0x1f;
constexpr std::uint8_t kThreeByteLeadMask = 0x0f;
constexpr std::uint8_t kFourByteLeadMask = 0x07;
constexpr std::uint8_t kUtf8ContinuationMask = 0xc0;
constexpr std::uint8_t kUtf8ContinuationValue = 0x80;
constexpr std::uint8_t kUtf8PayloadMask = 0x3f;
constexpr std::size_t kUtf8PayloadBits = 6;
constexpr std::uint32_t kTwoByteMinValue = 0x80;
constexpr std::uint32_t kThreeByteMinValue = 0x800;
constexpr std::uint32_t kFourByteMinValue = 0x10000;
constexpr std::uint32_t kUnicodeMax = 0x10ffff;
constexpr std::uint32_t kSurrogatePairUnits = 2;

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
  return static_cast<std::uint16_t>(std::get<std::uint8_t>(low) |
                                    (std::get<std::uint8_t>(high) << 8));
}

std::variant<std::uint32_t, MetadataError> MetadataCursor::U32() {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto byte = U8();
    if (std::holds_alternative<MetadataError>(byte))
      return std::get<MetadataError>(byte);
    value |= static_cast<std::uint32_t>(std::get<std::uint8_t>(byte))
             << (i * kBitsPerByte);
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
        ((raw & kContinuation) || (raw & kPayloadMask) > kFinalUlebPayloadMax))
      return MetadataError::kMalformedLeb;
    value |= static_cast<std::uint32_t>(raw & kPayloadMask) << (i * kLebBits);
    if (!(raw & kContinuation)) return value;
  }
  return MetadataError::kMalformedLeb;
}

std::variant<std::uint32_t, MetadataError> MetadataCursor::SlebBits() {
  std::uint32_t bits = 0;
  for (std::size_t i = 0; i < kMaxLebBytes; ++i) {
    const auto byte = U8();
    if (std::holds_alternative<MetadataError>(byte))
      return std::get<MetadataError>(byte);
    const auto raw = std::get<std::uint8_t>(byte);
    if (i == kMaxLebBytes - 1) {
      const auto high = raw & kSlebHighMask;
      const auto sign = raw & kSlebSignBit;
      if ((raw & kContinuation) || (high != (sign ? kSlebHighMask : 0)))
        return MetadataError::kMalformedLeb;
    }
    bits |= static_cast<std::uint32_t>(raw & kPayloadMask) << (i * kLebBits);
    if (!(raw & kContinuation)) {
      constexpr std::uint8_t kLebSign = 0x40;
      if (i < kMaxLebBytes - 1 && (raw & kLebSign))
        bits |= UINT32_MAX << ((i + 1) * kLebBits);
      return bits;
    }
  }
  return MetadataError::kMalformedLeb;
}

bool MetadataCursor::Skip(std::uint64_t count) {
  if (count > limit_ - position_) return false;
  position_ += static_cast<std::uint32_t>(count);
  return true;
}

std::variant<std::string, MetadataError> ReadMetadataString(
    MetadataCursor* cursor, std::uint32_t* utf16_length, std::size_t* spent,
    std::size_t budget) {
  const auto prefix = cursor->Uleb();
  if (std::holds_alternative<MetadataError>(prefix))
    return std::get<MetadataError>(prefix);
  const auto encoded = std::get<std::uint32_t>(prefix);
  *utf16_length = encoded >> kStringLengthShift;
  const bool ascii = (encoded & kStringAsciiFlag) != 0;
  std::string value;
  std::size_t charged_capacity = 0;
  const auto append = [&](std::uint8_t byte) -> bool {
    if (value.size() >= kMaxStringBytes) return false;
    if (value.size() == value.capacity()) {
      const std::size_t old_capacity = value.capacity();
      const std::size_t target =
          old_capacity > (std::numeric_limits<std::size_t>::max() - 1) / 2
              ? std::numeric_limits<std::size_t>::max()
              : old_capacity * 2 + 1;
      // During reserve, both old and new heap buffers may be live.
      if (target >= budget - *spent) return false;
      value.reserve(target);
      const auto new_capacity = value.capacity();
      if (new_capacity >= budget - *spent) return false;
      const auto new_charge = new_capacity + 1;
      *spent += new_charge - charged_capacity;
      charged_capacity = new_charge;
    }
    value.push_back(static_cast<char>(byte));
    return true;
  };
  std::uint32_t units = 0;
  for (;;) {
    const auto byte = cursor->U8();
    if (std::holds_alternative<MetadataError>(byte))
      return std::get<MetadataError>(byte);
    const auto raw = std::get<std::uint8_t>(byte);
    if (raw == 0) {
      if (units != *utf16_length) return MetadataError::kBadString;
      return value;
    }
    if (ascii && raw > kAsciiMax) return MetadataError::kBadString;
    if (!append(raw)) return MetadataError::kResourceLimit;
    std::uint32_t scalar_units = 1;
    if (raw >= kUtf8ContinuationValue) {
      if (raw < kTwoByteLeadMin || raw > kFourByteLeadMax)
        return MetadataError::kBadString;
      const std::size_t followers = raw < kThreeByteLeadMin  ? 1
                                    : raw < kFourByteLeadMin ? 2
                                                             : 3;
      std::uint32_t decoded = raw & (followers == 1   ? kTwoByteLeadMask
                                     : followers == 2 ? kThreeByteLeadMask
                                                      : kFourByteLeadMask);
      for (std::size_t part = 0; part < followers; ++part) {
        const auto next = cursor->U8();
        if (std::holds_alternative<MetadataError>(next))
          return std::get<MetadataError>(next);
        const auto continuation = std::get<std::uint8_t>(next);
        if ((continuation & kUtf8ContinuationMask) != kUtf8ContinuationValue)
          return MetadataError::kBadString;
        decoded =
            (decoded << kUtf8PayloadBits) | (continuation & kUtf8PayloadMask);
        if (!append(continuation)) return MetadataError::kResourceLimit;
      }
      if ((followers == 1 && decoded < kTwoByteMinValue && decoded != 0) ||
          (followers == 2 && decoded < kThreeByteMinValue) ||
          (followers == 3 &&
           (decoded < kFourByteMinValue || decoded > kUnicodeMax)))
        return MetadataError::kBadString;
      if (followers == 3) scalar_units = kSurrogatePairUnits;
    }
    units += scalar_units;
  }
}

}  // namespace abcd::abc
