#include "abcd/abc/byte_reader.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <variant>

namespace {

using abcd::abc::ByteReader;
using abcd::abc::ByteReaderError;

void Check(const bool condition) {
  if (!condition) {
    std::abort();
  }
}

template <typename T>
const T& Value(const std::variant<T, ByteReaderError>& result) {
  Check(std::holds_alternative<T>(result));
  return std::get<T>(result);
}

template <typename T>
void ExpectError(const std::variant<T, ByteReaderError>& result,
                 const ByteReaderError error) {
  Check(std::holds_alternative<ByteReaderError>(result));
  Check(std::get<ByteReaderError>(result) == error);
}

void ReadsLittleEndianU32AndConsumesExactEnd() {
  constexpr std::uint8_t kBytes[] = {0x78, 0x56, 0x34, 0x12};
  ByteReader reader(kBytes);

  Check(Value(reader.ReadU32Le()) == 0x12345678U);
  Check(reader.Position() == std::size(kBytes));
  Check(reader.AtEnd());
}

void ReadsNonemptySliceWithoutCopying() {
  constexpr std::uint8_t kBytes[] = {0x10, 0x20, 0x30, 0x40};
  ByteReader reader(kBytes);

  const std::span<const std::uint8_t> bytes = Value(reader.ReadBytes(2));
  Check(bytes.data() == kBytes);
  Check(bytes.size() == 2);
  Check(bytes[0] == 0x10);
  Check(bytes[1] == 0x20);
  Check(reader.Position() == 2);
  Check(!reader.AtEnd());

  const std::span<const std::uint8_t> tail = Value(reader.ReadBytes(2));
  Check(tail.data() == kBytes + 2);
  Check(tail[0] == 0x30);
  Check(tail[1] == 0x40);
  Check(reader.Position() == std::size(kBytes));
  Check(reader.AtEnd());
}

void ReadsEmptySliceAtNonzeroEnd() {
  constexpr std::uint8_t kBytes[] = {0xaa};
  ByteReader reader(kBytes);

  Check(Value(reader.ReadBytes(1))[0] == 0xaa);

  const std::span<const std::uint8_t> bytes = Value(reader.ReadBytes(0));
  Check(bytes.empty());
  Check(reader.Position() == 1);
  Check(reader.AtEnd());
}

void RejectsHugeSliceWithoutAdvancing() {
  constexpr std::uint8_t kBytes[] = {0x10};
  ByteReader reader(kBytes);

  ExpectError(reader.ReadBytes(SIZE_MAX), ByteReaderError::kTruncated);
  Check(reader.Position() == 0);
}

void RejectsTruncatedU32AtNonzeroCursorWithoutAdvancing() {
  constexpr std::uint8_t kBytes[] = {0xaa, 0x10, 0x20, 0x30};
  ByteReader reader(kBytes);

  Check(Value(reader.ReadBytes(1))[0] == 0xaa);
  ExpectError(reader.ReadU32Le(), ByteReaderError::kTruncated);
  Check(reader.Position() == 1);
}

void RejectsTruncatedSliceAtNonzeroCursorWithoutAdvancing() {
  constexpr std::uint8_t kBytes[] = {0xaa, 0x10};
  ByteReader reader(kBytes);

  Check(Value(reader.ReadBytes(1))[0] == 0xaa);
  ExpectError(reader.ReadBytes(2), ByteReaderError::kTruncated);
  Check(reader.Position() == 1);
}

void ReadsZeroMaximumAndNoncanonicalUleb128() {
  constexpr std::uint8_t kZero[] = {0x00};
  constexpr std::uint8_t kMaximum[] = {0xff, 0xff, 0xff, 0xff, 0x0f};
  constexpr std::uint8_t kNoncanonical[] = {0x80, 0x00};

  ByteReader zero_reader(kZero);
  ByteReader maximum_reader(kMaximum);
  ByteReader noncanonical_reader(kNoncanonical);

  Check(Value(zero_reader.ReadUleb128U32()) == 0U);
  Check(Value(maximum_reader.ReadUleb128U32()) == UINT32_MAX);
  Check(Value(noncanonical_reader.ReadUleb128U32()) == 0U);
}

void RejectsFifthBytePayloadOverflowWithoutAdvancing() {
  constexpr std::uint8_t kBytes[] = {0x80, 0x80, 0x80, 0x80, 0x10};
  ByteReader reader(kBytes);

  ExpectError(reader.ReadUleb128U32(), ByteReaderError::kOverflow);
  Check(reader.Position() == 0);
}

void RejectsFifthByteContinuationWithoutAdvancing() {
  constexpr std::uint8_t kBytes[] = {0x80, 0x80, 0x80, 0x80, 0x80};
  ByteReader reader(kBytes);

  ExpectError(reader.ReadUleb128U32(), ByteReaderError::kOverflow);
  Check(reader.Position() == 0);
}

void RejectsTruncatedUleb128AtNonzeroCursorWithoutAdvancing() {
  constexpr std::uint8_t kBytes[] = {0xaa, 0x80};
  ByteReader reader(kBytes);

  Check(Value(reader.ReadBytes(1))[0] == 0xaa);
  ExpectError(reader.ReadUleb128U32(), ByteReaderError::kTruncated);
  Check(reader.Position() == 1);
}

void RejectsUleb128OverflowAtNonzeroCursorWithoutAdvancing() {
  constexpr std::uint8_t kBytes[] = {0xaa, 0x80, 0x80, 0x80, 0x80, 0x10};
  ByteReader reader(kBytes);

  const std::span<const std::uint8_t> prefix = Value(reader.ReadBytes(1));
  Check(prefix[0] == 0xaa);
  ExpectError(reader.ReadUleb128U32(), ByteReaderError::kOverflow);
  Check(reader.Position() == 1);
}

}  // namespace

int main() {
  ReadsLittleEndianU32AndConsumesExactEnd();
  ReadsNonemptySliceWithoutCopying();
  ReadsEmptySliceAtNonzeroEnd();
  RejectsHugeSliceWithoutAdvancing();
  RejectsTruncatedU32AtNonzeroCursorWithoutAdvancing();
  RejectsTruncatedSliceAtNonzeroCursorWithoutAdvancing();
  ReadsZeroMaximumAndNoncanonicalUleb128();
  RejectsFifthBytePayloadOverflowWithoutAdvancing();
  RejectsFifthByteContinuationWithoutAdvancing();
  RejectsTruncatedUleb128AtNonzeroCursorWithoutAdvancing();
  RejectsUleb128OverflowAtNonzeroCursorWithoutAdvancing();
  return 0;
}
