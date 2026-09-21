#include "abcd/abc/header.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <variant>

#include "abcd/abc/byte_reader.h"

namespace abcd::abc {
namespace {

// Pinned source: runtime_core 3e147bb libpandafile/file.h and
// docs/file_format.md. Static offsets are from static_core/libarkfile/file.h.
constexpr std::array<std::uint8_t, 8> kMagic = {'P', 'A', 'N', 'D',
                                                'A', 0,   0,   0};
constexpr AbcVersion kDynamic9 = {{9, 0, 0, 0}};
constexpr AbcVersion kDynamic1100 = {{11, 0, 0, 0}};
constexpr AbcVersion kDynamic1101 = {{11, 0, 1, 0}};
constexpr AbcVersion kDynamic1102 = {{11, 0, 2, 0}};
constexpr AbcVersion kDynamic1202 = {{12, 0, 2, 0}};
constexpr AbcVersion kDynamic1206 = {{12, 0, 6, 0}};
constexpr AbcVersion kDynamic1301 = {{13, 0, 1, 0}};
constexpr AbcVersion kDynamic2400 = {{24, 0, 0, 0}};
constexpr AbcVersion kStatic0006 = {{0, 0, 0, 6}};
constexpr AbcVersion kStatic0107 = {{0, 1, 0, 7}};
constexpr std::uint32_t kIndexEntrySize = 4;
constexpr std::uint32_t kRegionHeaderSize = 40;
constexpr std::uint32_t kReservedLiteralWord = UINT32_MAX;
constexpr std::size_t kCommonPrefixSize = 28;
constexpr std::size_t kVersionWidth = 4;

template <typename T>
bool IsReaderError(const ByteReaderResult<T>& value) {
  return std::holds_alternative<ByteReaderError>(value);
}

HeaderResult ReadCommon(const std::span<const std::uint8_t> prefix,
                        const std::uint64_t actual_size) {
  if (actual_size > UINT32_MAX) {
    return HeaderError::kActualSizeOverflow;
  }

  ByteReader reader(prefix);
  const auto magic = reader.ReadBytes(kMagic.size());
  if (IsReaderError(magic)) {
    return HeaderError::kTruncated;
  }
  const std::span<const std::uint8_t> magic_bytes =
      std::get<std::span<const std::uint8_t>>(magic);
  std::array<std::uint8_t, 8> parsed_magic{};
  for (std::size_t index = 0; index < kMagic.size(); ++index) {
    parsed_magic[index] = magic_bytes[index];
  }
  if (parsed_magic != kMagic) {
    return HeaderError::kBadMagic;
  }

  const auto checksum = reader.ReadU32Le();
  const auto version_bytes = reader.ReadBytes(kVersionWidth);
  const auto file_size = reader.ReadU32Le();
  const auto foreign_offset = reader.ReadU32Le();
  const auto foreign_size = reader.ReadU32Le();
  if (IsReaderError(checksum) || IsReaderError(version_bytes) ||
      IsReaderError(file_size) || IsReaderError(foreign_offset) ||
      IsReaderError(foreign_size)) {
    return HeaderError::kTruncated;
  }

  Header header;
  header.magic = parsed_magic;
  header.checksum = std::get<std::uint32_t>(checksum);
  const std::span<const std::uint8_t> raw_version =
      std::get<std::span<const std::uint8_t>>(version_bytes);
  for (std::size_t index = 0; index < header.version.bytes.size(); ++index) {
    header.version.bytes[index] = raw_version[index];
  }
  header.file_size = std::get<std::uint32_t>(file_size);
  header.foreign_offset = std::get<std::uint32_t>(foreign_offset);
  header.foreign_size = std::get<std::uint32_t>(foreign_size);
  if (header.file_size != actual_size) {
    return HeaderError::kFileSizeMismatch;
  }
  if (header.foreign_offset > header.file_size ||
      header.foreign_size > header.file_size - header.foreign_offset) {
    return HeaderError::kForeignRange;
  }
  header.layout = UnknownLayout{};
  return header;
}

bool ReadU32(ByteReader* reader, std::uint32_t* value) {
  const auto read = reader->ReadU32Le();
  if (IsReaderError(read)) {
    return false;
  }
  *value = std::get<std::uint32_t>(read);
  return true;
}

HeaderResult ReadDynamicLayout(const std::span<const std::uint8_t> prefix,
                               Header header) {
  if (prefix.size() < kDynamicHeaderSize) {
    return HeaderError::kTruncated;
  }
  ByteReader reader(prefix.subspan(kCommonPrefixSize));
  DynamicLayout layout;
  if (!ReadU32(&reader, &layout.classes.count) ||
      !ReadU32(&reader, &layout.classes.offset) ||
      !ReadU32(&reader, &layout.line_number_programs.count) ||
      !ReadU32(&reader, &layout.line_number_programs.offset) ||
      !ReadU32(&reader, &layout.literal_arrays.count) ||
      !ReadU32(&reader, &layout.literal_arrays.offset) ||
      !ReadU32(&reader, &layout.index_regions.count) ||
      !ReadU32(&reader, &layout.index_regions.offset)) {
    return HeaderError::kTruncated;
  }
  header.layout = layout;
  return header;
}

HeaderResult ReadStaticLayout(const std::span<const std::uint8_t> prefix,
                              Header header) {
  if (prefix.size() < kStaticHeaderSize) {
    return HeaderError::kTruncated;
  }
  ByteReader reader(prefix.subspan(kCommonPrefixSize));
  StaticLayout layout;
  if (!ReadU32(&reader, &layout.quickened_flag) ||
      !ReadU32(&reader, &layout.classes.count) ||
      !ReadU32(&reader, &layout.classes.offset) ||
      !ReadU32(&reader, &layout.export_table.count) ||
      !ReadU32(&reader, &layout.export_table.offset) ||
      !ReadU32(&reader, &layout.line_number_programs.count) ||
      !ReadU32(&reader, &layout.line_number_programs.offset) ||
      !ReadU32(&reader, &layout.literal_arrays.count) ||
      !ReadU32(&reader, &layout.literal_arrays.offset) ||
      !ReadU32(&reader, &layout.index_regions.count) ||
      !ReadU32(&reader, &layout.index_regions.offset)) {
    return HeaderError::kTruncated;
  }
  header.layout = layout;
  return header;
}

std::optional<HeaderError> ValidateSection(const IndexSection section,
                                           const std::uint32_t element_size,
                                           const std::uint32_t header_size,
                                           const std::uint32_t file_size) {
  if (section.offset != 0 && section.offset % kIndexEntrySize != 0) {
    return HeaderError::kIndexAlignment;
  }
  if (section.offset > file_size) {
    return HeaderError::kIndexRange;
  }
  if (section.count == 0) {
    return std::nullopt;
  }
  if (section.offset < header_size ||
      section.count > (file_size - section.offset) / element_size) {
    return HeaderError::kIndexRange;
  }
  return std::nullopt;
}

HeaderResult ValidateDynamic(Header header) {
  const auto& layout = std::get<DynamicLayout>(header.layout);
  const std::array<IndexSection, 3> word_indexes = {
      layout.classes, layout.line_number_programs, layout.literal_arrays};
  for (const IndexSection section : word_indexes) {
    if (section.count == 0) {
      if (section.offset != 0 && section.offset % kIndexEntrySize != 0) {
        return HeaderError::kIndexAlignment;
      }
      if (section.offset > header.file_size) {
        return HeaderError::kIndexRange;
      }
      continue;
    }
    const std::optional<HeaderError> error = ValidateSection(
        section, kIndexEntrySize, kDynamicHeaderSize, header.file_size);
    if (error.has_value()) {
      return *error;
    }
  }
  const IndexSection regions = layout.index_regions;
  if (regions.count == 0) {
    if ((regions.offset != 0 && regions.offset % kIndexEntrySize != 0) ||
        regions.offset > header.file_size) {
      return regions.offset > header.file_size ? HeaderError::kIndexRange
                                               : HeaderError::kIndexAlignment;
    }
    return header;
  }
  const std::optional<HeaderError> error = ValidateSection(
      regions, kRegionHeaderSize, kDynamicHeaderSize, header.file_size);
  return error.has_value() ? HeaderResult(*error) : HeaderResult(header);
}

HeaderResult ValidateStatic(Header header, const bool legacy_export_count) {
  const auto& layout = std::get<StaticLayout>(header.layout);
  const std::array<IndexSection, 3> word_indexes = {
      layout.classes, layout.line_number_programs, layout.literal_arrays};
  for (const IndexSection section : word_indexes) {
    if (section.count == 0) {
      if ((section.offset != 0 && section.offset % kIndexEntrySize != 0) ||
          section.offset > header.file_size) {
        return section.offset > header.file_size ? HeaderError::kIndexRange
                                                 : HeaderError::kIndexAlignment;
      }
      continue;
    }
    const std::optional<HeaderError> error = ValidateSection(
        section, kIndexEntrySize, kStaticHeaderSize, header.file_size);
    if (error.has_value()) {
      return *error;
    }
  }
  const std::uint32_t export_width = legacy_export_count ? kIndexEntrySize : 1;
  const std::array<IndexSection, 2> remaining = {layout.export_table,
                                                 layout.index_regions};
  const std::array<std::uint32_t, 2> widths = {export_width, kRegionHeaderSize};
  for (std::size_t index = 0; index < remaining.size(); ++index) {
    const IndexSection section = remaining[index];
    if (section.count == 0) {
      if ((section.offset != 0 && section.offset % kIndexEntrySize != 0) ||
          section.offset > header.file_size) {
        return section.offset > header.file_size ? HeaderError::kIndexRange
                                                 : HeaderError::kIndexAlignment;
      }
      continue;
    }
    const std::optional<HeaderError> error = ValidateSection(
        section, widths[index], kStaticHeaderSize, header.file_size);
    if (error.has_value()) {
      return *error;
    }
  }
  return header;
}

ProfileId DynamicProfile(const AbcVersion version) {
  if (version == kDynamic9) return ProfileId::kDynamic9;
  if (version == kDynamic1102) return ProfileId::kDynamic1102;
  if (version == kDynamic1202) return ProfileId::kDynamic1202;
  if (version == kDynamic1206) return ProfileId::kDynamic1206;
  if (version == kDynamic1301) return ProfileId::kDynamic1301;
  if (version == kDynamic2400) return ProfileId::kDynamic2400;
  return ProfileId::kUnknown;
}

}  // namespace

HeaderResult ParseHeader(const std::span<const std::uint8_t> prefix,
                         const std::uint64_t actual_size) {
  HeaderResult common = ReadCommon(prefix, actual_size);
  if (!std::holds_alternative<Header>(common)) {
    return common;
  }
  Header header = std::get<Header>(common);
  if (header.file_size < kDynamicHeaderSize) {
    return HeaderError::kFileSizeMismatch;
  }
  if (header.version == kDynamic1100 || header.version == kDynamic1101) {
    return HeaderError::kIncompatibleVersion;
  }
  const ProfileId profile = DynamicProfile(header.version);
  if (profile != ProfileId::kUnknown) {
    header.kind = AbcKind::kDynamic;
    header.profile = profile;
    HeaderResult parsed = ReadDynamicLayout(prefix, header);
    if (!std::holds_alternative<Header>(parsed)) return parsed;
    header = std::get<Header>(parsed);
    const auto& layout = std::get<DynamicLayout>(header.layout);
    if (header.version > kDynamic1206 &&
        (layout.literal_arrays.count != kReservedLiteralWord ||
         layout.literal_arrays.offset != kReservedLiteralWord)) {
      return HeaderError::kReservedLiteralFields;
    }
    if (header.version > kDynamic1206) {
      const DynamicLayout validation_layout = {layout.classes,
                                               layout.line_number_programs,
                                               {0, 0},
                                               layout.index_regions};
      Header validation_header = header;
      validation_header.layout = validation_layout;
      HeaderResult validated = ValidateDynamic(validation_header);
      return std::holds_alternative<Header>(validated) ? HeaderResult(header)
                                                       : validated;
    }
    return ValidateDynamic(header);
  }
  if (header.version == kStatic0107) {
    if (header.file_size < kStaticHeaderSize) {
      return HeaderError::kFileSizeMismatch;
    }
    header.kind = AbcKind::kStatic;
    header.profile = ProfileId::kStatic0107;
    HeaderResult parsed = ReadStaticLayout(prefix, header);
    if (!std::holds_alternative<Header>(parsed)) return parsed;
    return ValidateStatic(std::get<Header>(parsed), false);
  }
  return header;
}

HeaderResult ParseStaticHeader(const std::span<const std::uint8_t> prefix,
                               const std::uint64_t actual_size) {
  HeaderResult common = ReadCommon(prefix, actual_size);
  if (!std::holds_alternative<Header>(common)) return common;
  Header header = std::get<Header>(common);
  if (header.version != kStatic0107 && header.version != kStatic0006) {
    return HeaderError::kStaticCandidateVersion;
  }
  const bool legacy = header.version == kStatic0006;
  if (header.file_size < kStaticHeaderSize) {
    return HeaderError::kFileSizeMismatch;
  }
  header.kind = legacy ? AbcKind::kUnknown : AbcKind::kStatic;
  header.profile = legacy ? ProfileId::kUnknown : ProfileId::kStatic0107;
  HeaderResult parsed = ReadStaticLayout(prefix, header);
  if (!std::holds_alternative<Header>(parsed)) return parsed;
  return ValidateStatic(std::get<Header>(parsed), legacy);
}

}  // namespace abcd::abc
