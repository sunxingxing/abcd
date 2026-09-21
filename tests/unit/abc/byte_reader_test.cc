#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

#include "abcd/abc/byte_reader.h"

namespace {

using abcd::abc::ByteReader;
using abcd::abc::ByteReaderError;

template <typename T>
const T& Value(const std::variant<T, ByteReaderError>& result) {
  assert(std::holds_alternative<T>(result));
  return std::get<T>(result);
}

template <typename T>
void ExpectError(const std::variant<T, ByteReaderError>& result,
                 const ByteReaderError error) {
  assert(std::holds_alternative<ByteReaderError>(result));
  assert(std::get<ByteReaderError>(result) == error);
}

void ReadsLittleEndianU32AndConsumesExactEnd() {
  constexpr std::uint8_t kBytes[] = {0x78, 0x56, 0x34, 0x12};
  ByteReader reader(kBytes);

  assert(Value(reader.ReadU32Le()) == 0x12345678U);
  assert(reader.Position() == std::size(kBytes));
  assert(reader.AtEnd());
}

void ReadsEmptySliceAtEnd() {
  ByteReader reader{std::span<const std::uint8_t>()};

  const std::span<const std::uint8_t> bytes = Value(reader.ReadBytes(0));
  assert(bytes.empty());
  assert(reader.Position() == 0);
  assert(reader.AtEnd());
}

void RejectsHugeSliceWithoutAdvancing() {
  constexpr std::uint8_t kBytes[] = {0x10};
  ByteReader reader(kBytes);

  ExpectError(reader.ReadBytes(SIZE_MAX), ByteReaderError::kTruncated);
  assert(reader.Position() == 0);
}

void RejectsTruncatedU32WithoutAdvancing() {
  constexpr std::uint8_t kBytes[] = {0x10, 0x20, 0x30};
  ByteReader reader(kBytes);

  ExpectError(reader.ReadU32Le(), ByteReaderError::kTruncated);
  assert(reader.Position() == 0);
}

void ReadsZeroMaximumAndNoncanonicalUleb128() {
  constexpr std::uint8_t kZero[] = {0x00};
  constexpr std::uint8_t kMaximum[] = {0xff, 0xff, 0xff, 0xff, 0x0f};
  constexpr std::uint8_t kNoncanonical[] = {0x80, 0x00};

  ByteReader zero_reader(kZero);
  ByteReader maximum_reader(kMaximum);
  ByteReader noncanonical_reader(kNoncanonical);

  assert(Value(zero_reader.ReadUleb128U32()) == 0U);
  assert(Value(maximum_reader.ReadUleb128U32()) == UINT32_MAX);
  assert(Value(noncanonical_reader.ReadUleb128U32()) == 0U);
}

void RejectsFifthBytePayloadOverflowWithoutAdvancing() {
  constexpr std::uint8_t kBytes[] = {0x80, 0x80, 0x80, 0x80, 0x10};
  ByteReader reader(kBytes);

  ExpectError(reader.ReadUleb128U32(), ByteReaderError::kOverflow);
  assert(reader.Position() == 0);
}

void RejectsFifthByteContinuationWithoutAdvancing() {
  constexpr std::uint8_t kBytes[] = {0x80, 0x80, 0x80, 0x80, 0x80};
  ByteReader reader(kBytes);

  ExpectError(reader.ReadUleb128U32(), ByteReaderError::kOverflow);
  assert(reader.Position() == 0);
}

void RejectsTruncatedUleb128AtNonzeroCursorWithoutAdvancing() {
  constexpr std::uint8_t kBytes[] = {0xaa, 0x80};
  ByteReader reader(kBytes);

  const std::span<const std::uint8_t> prefix = Value(reader.ReadBytes(1));
  assert(prefix[0] == 0xaa);
  ExpectError(reader.ReadUleb128U32(), ByteReaderError::kTruncated);
  assert(reader.Position() == 1);
}

}  // namespace

int main() {
  ReadsLittleEndianU32AndConsumesExactEnd();
  ReadsEmptySliceAtEnd();
  RejectsHugeSliceWithoutAdvancing();
  RejectsTruncatedU32WithoutAdvancing();
  ReadsZeroMaximumAndNoncanonicalUleb128();
  RejectsFifthBytePayloadOverflowWithoutAdvancing();
  RejectsFifthByteContinuationWithoutAdvancing();
  RejectsTruncatedUleb128AtNonzeroCursorWithoutAdvancing();
  return 0;
}
