#include "abcd/abc/byte_reader.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace abcd::abc {
namespace {

// OpenHarmony runtime_core: libpandabase/include/libpandabase/utils/leb128.h
// and docs/file_format.md define these fixed-width and ULEB128 encoding limits.
constexpr std::size_t kU32Width = 4;
constexpr std::size_t kBitsPerByte = 8;
constexpr std::size_t kUleb128U32MaxBytes = 5;
constexpr std::uint8_t kUleb128PayloadMask = 0x7f;
constexpr std::uint8_t kUleb128ContinuationMask = 0x80;
constexpr std::uint8_t kUleb128FifthByteMaxPayload = 0x0f;
constexpr std::size_t kUleb128BitsPerByte = 7;

}  // namespace

ByteReader::ByteReader(const std::span<const std::uint8_t> bytes)
    : bytes_(bytes) {}

ByteReaderResult<std::uint32_t> ByteReader::ReadU32Le() {
  if (kU32Width > bytes_.size() - position_) {
    return ByteReaderError::kTruncated;
  }

  std::uint32_t value = 0;
  for (std::size_t index = 0; index < kU32Width; ++index) {
    value |= static_cast<std::uint32_t>(bytes_[position_ + index])
             << (index * kBitsPerByte);
  }
  position_ += kU32Width;
  return value;
}

ByteReaderResult<std::span<const std::uint8_t>> ByteReader::ReadBytes(
    const std::size_t length) {
  if (length > bytes_.size() - position_) {
    return ByteReaderError::kTruncated;
  }

  const std::span<const std::uint8_t> result =
      bytes_.subspan(position_, length);
  position_ += length;
  return result;
}

ByteReaderResult<std::uint32_t> ByteReader::ReadUleb128U32() {
  std::uint32_t value = 0;

  // Commit only after validating every byte, so failures preserve position_.
  for (std::size_t index = 0; index < kUleb128U32MaxBytes; ++index) {
    if (index >= bytes_.size() - position_) {
      return ByteReaderError::kTruncated;
    }

    const std::uint8_t byte = bytes_[position_ + index];
    const std::uint8_t payload = byte & kUleb128PayloadMask;
    const bool has_continuation = (byte & kUleb128ContinuationMask) != 0;
    if (index == kUleb128U32MaxBytes - 1) {
      if (has_continuation || payload > kUleb128FifthByteMaxPayload) {
        return ByteReaderError::kOverflow;
      }
    }

    value |= static_cast<std::uint32_t>(payload)
             << (index * kUleb128BitsPerByte);
    if (!has_continuation) {
      position_ += index + 1;
      return value;
    }
  }

  return ByteReaderError::kOverflow;
}

std::size_t ByteReader::Position() const { return position_; }

bool ByteReader::AtEnd() const { return position_ == bytes_.size(); }

}  // namespace abcd::abc
