#include <zlib.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

#include "abcd/abc/header.h"

namespace {
constexpr std::size_t kChecksumOffset = 8;
constexpr std::size_t kVersionOffset = 12;
constexpr std::size_t kFileSizeOffset = 16;
constexpr std::size_t kClassCountOffset = 28;
constexpr std::size_t kClassTableOffset = 32;
constexpr std::size_t kRegionCountOffset = 52;
constexpr std::size_t kRegionTableOffset = 56;
constexpr std::size_t kRegionBytes = 40;
constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kRegionOffset =
    abcd::abc::kDynamicHeaderSize + kWordBytes;
constexpr std::uint32_t kClassOffset = kRegionOffset + kRegionBytes;
enum class FixtureKind { kEmptyRecord, kMethod, kInvalidMethod };

void WriteWord(std::span<std::uint8_t> bytes, std::size_t offset,
               std::uint32_t value) {
  for (std::size_t i = 0; i < kWordBytes; ++i)
    bytes[offset + i] = value >> (i * kByteBits);
}

bool Write(const std::filesystem::path& path, std::vector<std::uint8_t> bytes,
           abcd::abc::AbcVersion version) {
  constexpr std::array<std::uint8_t, 8> kMagic{'P', 'A', 'N', 'D',
                                               'A', 0,   0,   0};
  std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
  std::copy(version.bytes.begin(), version.bytes.end(),
            bytes.begin() + kVersionOffset);
  WriteWord(bytes, kFileSizeOffset, bytes.size());
  WriteWord(
      bytes, kChecksumOffset,
      adler32(1, bytes.data() + kVersionOffset, bytes.size() - kVersionOffset));
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  output.close();
  return !output.fail();
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) return EXIT_FAILURE;
  FixtureKind kind = FixtureKind::kEmptyRecord;
  if (argc == 3) {
    const std::string_view option(argv[2]);
    if (option == "--method")
      kind = FixtureKind::kMethod;
    else if (option == "--invalid-method")
      kind = FixtureKind::kInvalidMethod;
    else
      return EXIT_FAILURE;
  }
  const std::filesystem::path root(argv[1]);
  std::filesystem::create_directories(root);
  // A local class with no members. Encoded UTF-16 length includes ASCII flag;
  // superclass, flags, field count, method count and terminating tag are zero.
  constexpr std::array<std::uint8_t, 16> kClass{
      13, 'L', 'd', 'e', 'm', 'o', ';', 0, 0, 0, 0, 0, 0, 0, 0, 0};
  std::vector<std::uint8_t> dynamic(kClassOffset);
  dynamic.insert(dynamic.end(), kClass.begin(), kClass.end());
  if (kind != FixtureKind::kEmptyRecord) {
    constexpr std::size_t kMethodCountInClass = 14;
    constexpr std::size_t kNameOffsetInMethod = 4;
    constexpr std::size_t kCodeOffsetInMethod = 10;
    // u16 owner, absent proto, name offset, flags, CODE + offset, end.
    constexpr std::array<std::uint8_t, 15> kMethod{0, 0, 0xff, 0xff, 0, 0, 0, 0,
                                                   0, 1, 0,    0,    0, 0, 0};
    const auto method_offset = dynamic.size();
    dynamic[kClassOffset + kMethodCountInClass] = 1;
    dynamic.insert(dynamic.end(), kMethod.begin(), kMethod.end());
    const auto name_offset = dynamic.size();
    dynamic.insert(dynamic.end(), {9, 'm', 'a', 'i', 'n', 0});
    const auto code_offset = dynamic.size();
    // Zero frame slots, one instruction, no tries. 0x65 is returnundefined;
    // 0xff is unassigned in the pinned dynamic ISA, not a static-file marker.
    dynamic.insert(dynamic.end(),
                   {0, 0, 1, 0,
                    static_cast<std::uint8_t>(
                        kind == FixtureKind::kMethod ? 0x65 : 0xff)});
    WriteWord(dynamic, method_offset + kNameOffsetInMethod, name_offset);
    WriteWord(dynamic, method_offset + kCodeOffsetInMethod, code_offset);
  }
  WriteWord(dynamic, kClassCountOffset, 1);
  WriteWord(dynamic, kClassTableOffset, abcd::abc::kDynamicHeaderSize);
  WriteWord(dynamic, abcd::abc::kDynamicHeaderSize, kClassOffset);
  WriteWord(dynamic, kRegionCountOffset, 1);
  WriteWord(dynamic, kRegionTableOffset, kRegionOffset);
  WriteWord(dynamic, kRegionOffset, kClassOffset);
  WriteWord(dynamic, kRegionOffset + kWordBytes, dynamic.size());
  WriteWord(dynamic, kRegionOffset + 2 * kWordBytes, 1);
  WriteWord(dynamic, kRegionOffset + 3 * kWordBytes,
            abcd::abc::kDynamicHeaderSize);
  if (!Write(root / "dynamic.abc", std::move(dynamic), {{12, 0, 6, 0}}) ||
      !Write(root / "static.abc",
             std::vector<std::uint8_t>(abcd::abc::kStaticHeaderSize),
             {{0, 1, 0, 7}}))
    return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
