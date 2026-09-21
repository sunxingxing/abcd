#include "abcd/abc/header.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <variant>

namespace {

using abcd::abc::AbcKind;
using abcd::abc::DynamicLayout;
using abcd::abc::Header;
using abcd::abc::HeaderError;
using abcd::abc::HeaderResult;
using abcd::abc::ParseHeader;
using abcd::abc::ParseStaticHeader;
using abcd::abc::ProfileId;
using abcd::abc::StaticLayout;
using abcd::abc::UnknownLayout;

constexpr std::size_t kDynamicHeaderSize = 60;
constexpr std::size_t kStaticHeaderSize = 72;
constexpr std::size_t kFileSizeOffset = 16;
constexpr std::size_t kForeignOffset = 20;
constexpr std::size_t kForeignSizeOffset = 24;
constexpr std::size_t kClassesCountOffset = 28;
constexpr std::size_t kClassesOffsetOffset = 32;
constexpr std::size_t kLnpsCountOffset = 36;
constexpr std::size_t kLnpsOffsetOffset = 40;
constexpr std::size_t kLiteralsCountOffset = 44;
constexpr std::size_t kLiteralsOffsetOffset = 48;
constexpr std::size_t kIndexesCountOffset = 52;
constexpr std::size_t kIndexesOffsetOffset = 56;
constexpr std::size_t kStaticClassesCountOffset = 32;
constexpr std::size_t kStaticClassesOffsetOffset = 36;
constexpr std::size_t kStaticExportsCountOffset = 40;
constexpr std::size_t kStaticExportsOffsetOffset = 44;
constexpr std::size_t kStaticLnpsCountOffset = 48;
constexpr std::size_t kStaticLnpsOffsetOffset = 52;
constexpr std::size_t kStaticLiteralsCountOffset = 56;
constexpr std::size_t kStaticLiteralsOffsetOffset = 60;
constexpr std::size_t kStaticIndexesCountOffset = 64;
constexpr std::size_t kStaticIndexesOffsetOffset = 68;

void Check(const bool condition) {
  if (!condition) {
    std::abort();
  }
}

void WriteU32(std::span<std::uint8_t> bytes, const std::size_t offset,
              const std::uint32_t value) {
  for (std::size_t index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
  }
}

template <std::size_t Size>
std::array<std::uint8_t, Size> MakeHeader(
    const std::array<std::uint8_t, 4> version) {
  std::array<std::uint8_t, Size> bytes{};
  bytes[0] = 'P';
  bytes[1] = 'A';
  bytes[2] = 'N';
  bytes[3] = 'D';
  bytes[4] = 'A';
  bytes[12] = version[0];
  bytes[13] = version[1];
  bytes[14] = version[2];
  bytes[15] = version[3];
  WriteU32(bytes, kFileSizeOffset, static_cast<std::uint32_t>(Size));
  return bytes;
}

Header Value(const HeaderResult& result) {
  Check(std::holds_alternative<Header>(result));
  return std::get<Header>(result);
}

void ExpectError(const HeaderResult& result, const HeaderError error) {
  Check(std::holds_alternative<HeaderError>(result));
  Check(std::get<HeaderError>(result) == error);
}

void RecognizesEveryDynamicProfile() {
  constexpr std::array<std::array<std::uint8_t, 4>, 6> kVersions = {{
      {9, 0, 0, 0},
      {11, 0, 2, 0},
      {12, 0, 2, 0},
      {12, 0, 6, 0},
      {13, 0, 1, 0},
      {24, 0, 0, 0},
  }};
  constexpr std::array<ProfileId, 6> kProfiles = {{
      ProfileId::kDynamic9,
      ProfileId::kDynamic1102,
      ProfileId::kDynamic1202,
      ProfileId::kDynamic1206,
      ProfileId::kDynamic1301,
      ProfileId::kDynamic2400,
  }};

  for (std::size_t index = 0; index < kVersions.size(); ++index) {
    auto bytes = MakeHeader<kDynamicHeaderSize>(kVersions[index]);
    if (kVersions[index][0] > 12) {
      WriteU32(bytes, kLiteralsCountOffset, UINT32_MAX);
      WriteU32(bytes, kLiteralsOffsetOffset, UINT32_MAX);
    }
    const Header header = Value(ParseHeader(bytes, bytes.size()));
    Check(header.kind == AbcKind::kDynamic);
    Check(header.profile == kProfiles[index]);
    Check(std::holds_alternative<DynamicLayout>(header.layout));
  }
}

void RecognizesStaticHeaderOnlyWithStaticLayout() {
  auto bytes = MakeHeader<kStaticHeaderSize>({0, 1, 0, 7});
  const Header header = Value(ParseHeader(bytes, bytes.size()));
  Check(header.kind == AbcKind::kStatic);
  Check(header.profile == ProfileId::kStatic0107);
  Check(std::holds_alternative<StaticLayout>(header.layout));
}

void DefaultParserKeepsLegacyStaticCandidateUnknown() {
  auto bytes = MakeHeader<kStaticHeaderSize>({0, 0, 0, 6});
  const Header header = Value(ParseHeader(bytes, bytes.size()));
  Check(header.kind == AbcKind::kUnknown);
  Check(header.profile == ProfileId::kUnknown);
  Check(std::holds_alternative<UnknownLayout>(header.layout));

  const Header candidate = Value(ParseStaticHeader(bytes, bytes.size()));
  Check(candidate.kind == AbcKind::kUnknown);
  Check(std::holds_alternative<StaticLayout>(candidate.layout));
}

void RejectsMalformedAndIncompatibleHeaders() {
  auto bytes = MakeHeader<kDynamicHeaderSize>({12, 0, 6, 0});
  bytes[0] = 'X';
  ExpectError(ParseHeader(bytes, bytes.size()), HeaderError::kBadMagic);

  auto complete = MakeHeader<kDynamicHeaderSize>({12, 0, 6, 0});
  std::array<std::uint8_t, kDynamicHeaderSize - 1> truncated{};
  for (std::size_t index = 0; index < truncated.size(); ++index) {
    truncated[index] = complete[index];
  }
  ExpectError(ParseHeader(truncated, complete.size()), HeaderError::kTruncated);

  bytes = MakeHeader<kDynamicHeaderSize>({11, 0, 0, 0});
  ExpectError(ParseHeader(bytes, bytes.size()),
              HeaderError::kIncompatibleVersion);

  bytes = MakeHeader<kDynamicHeaderSize>({12, 0, 6, 0});
  WriteU32(bytes, kFileSizeOffset, 61);
  ExpectError(ParseHeader(bytes, bytes.size()), HeaderError::kFileSizeMismatch);
  ExpectError(ParseHeader(bytes, static_cast<std::uint64_t>(UINT32_MAX) + 1),
              HeaderError::kActualSizeOverflow);
}

void ValidatesRangesAlignmentAndCountsWithoutOverflow() {
  auto bytes = MakeHeader<kDynamicHeaderSize>({12, 0, 6, 0});
  WriteU32(bytes, kForeignOffset, 56);
  WriteU32(bytes, kForeignSizeOffset, 8);
  ExpectError(ParseHeader(bytes, bytes.size()), HeaderError::kForeignRange);

  bytes = MakeHeader<kDynamicHeaderSize>({12, 0, 6, 0});
  WriteU32(bytes, kClassesCountOffset, 1);
  WriteU32(bytes, kClassesOffsetOffset, 61);
  ExpectError(ParseHeader(bytes, bytes.size()), HeaderError::kIndexAlignment);

  bytes = MakeHeader<kDynamicHeaderSize>({12, 0, 6, 0});
  WriteU32(bytes, kClassesCountOffset, UINT32_MAX);
  WriteU32(bytes, kClassesOffsetOffset, 60);
  ExpectError(ParseHeader(bytes, bytes.size()), HeaderError::kIndexRange);

  bytes = MakeHeader<kDynamicHeaderSize>({12, 0, 6, 0});
  WriteU32(bytes, kIndexesCountOffset, 1);
  WriteU32(bytes, kIndexesOffsetOffset, 60);
  ExpectError(ParseHeader(bytes, bytes.size()), HeaderError::kIndexRange);
}

void RejectsNonreservedNewDynamicLiteralWords() {
  auto bytes = MakeHeader<kDynamicHeaderSize>({13, 0, 1, 0});
  ExpectError(ParseHeader(bytes, bytes.size()),
              HeaderError::kReservedLiteralFields);
}

void PermitsEmptyIndexesAtZeroAndFileEnd() {
  auto bytes = MakeHeader<kDynamicHeaderSize>({12, 0, 6, 0});
  WriteU32(bytes, kClassesOffsetOffset, 0);
  WriteU32(bytes, kLnpsOffsetOffset, kDynamicHeaderSize);
  WriteU32(bytes, kLiteralsOffsetOffset, kDynamicHeaderSize);
  WriteU32(bytes, kIndexesOffsetOffset, 0);
  Value(ParseHeader(bytes, bytes.size()));
}

void ValidatesStaticIndexesAndExportByteSpan() {
  auto bytes = MakeHeader<kStaticHeaderSize>({0, 1, 0, 7});
  WriteU32(bytes, kFileSizeOffset, 73);
  WriteU32(bytes, kStaticExportsCountOffset, 1);
  WriteU32(bytes, kStaticExportsOffsetOffset, 72);
  Value(ParseHeader(bytes, 73));

  bytes = MakeHeader<kStaticHeaderSize>({0, 1, 0, 7});
  WriteU32(bytes, kStaticClassesCountOffset, 1);
  WriteU32(bytes, kStaticClassesOffsetOffset, 72);
  ExpectError(ParseHeader(bytes, bytes.size()), HeaderError::kIndexRange);

  bytes = MakeHeader<kStaticHeaderSize>({0, 0, 0, 6});
  WriteU32(bytes, kStaticExportsCountOffset, 1);
  WriteU32(bytes, kStaticExportsOffsetOffset, 72);
  ExpectError(ParseStaticHeader(bytes, bytes.size()), HeaderError::kIndexRange);
}

void UnknownVersionsOnlyValidateCommonFields() {
  auto bytes = MakeHeader<kDynamicHeaderSize>({42, 0, 0, 0});
  WriteU32(bytes, kClassesCountOffset, UINT32_MAX);
  WriteU32(bytes, kClassesOffsetOffset, 1);
  const Header header = Value(ParseHeader(bytes, bytes.size()));
  Check(header.kind == AbcKind::kUnknown);
  Check(std::holds_alternative<UnknownLayout>(header.layout));
}

}  // namespace

int main() {
  RecognizesEveryDynamicProfile();
  RecognizesStaticHeaderOnlyWithStaticLayout();
  DefaultParserKeepsLegacyStaticCandidateUnknown();
  RejectsMalformedAndIncompatibleHeaders();
  ValidatesRangesAlignmentAndCountsWithoutOverflow();
  RejectsNonreservedNewDynamicLiteralWords();
  PermitsEmptyIndexesAtZeroAndFileEnd();
  ValidatesStaticIndexesAndExportByteSpan();
  UnknownVersionsOnlyValidateCommonFields();
  return 0;
}
