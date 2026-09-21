#ifndef ABCD_ABC_BYTE_READER_H_
#define ABCD_ABC_BYTE_READER_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace abcd::abc {

enum class ByteReaderError {
  kTruncated,
  kOverflow,
};

template <typename T>
using ByteReaderResult = std::variant<T, ByteReaderError>;

class ByteReader {
 public:
  // The caller retains ownership of bytes and must keep them alive while reading.
  explicit ByteReader(std::span<const std::uint8_t> bytes);

  ByteReaderResult<std::uint32_t> ReadU32Le();
  ByteReaderResult<std::span<const std::uint8_t>> ReadBytes(
      std::size_t length);
  ByteReaderResult<std::uint32_t> ReadUleb128U32();

  std::size_t Position() const;
  bool AtEnd() const;

 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t position_ = 0;
};

}  // namespace abcd::abc

#endif  // ABCD_ABC_BYTE_READER_H_
