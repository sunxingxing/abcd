#include "abcd/corpus/detect.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace abcd::corpus {
namespace {
// Pinned runtime_core static_core/libarkfile/{file_items.cpp,
// class_data_accessor.cpp}: class names are StringItem records, followed by
// superclass, three ULEBs, and optional class tags.
constexpr abc::AbcVersion kLegacyStatic = {{0, 0, 0, 6}};
constexpr std::size_t kVersionOffset = 12;
constexpr std::size_t kVersionWidth = 4;
constexpr std::size_t kChecksumOffset = 8;
constexpr std::size_t kClassProbeBytes = 64 * 1024;
constexpr std::size_t kTotalProbeBytes = 1024 * 1024;
constexpr std::uint32_t kClassLimit = 256;
constexpr std::uint8_t kInterfacesTag = 1;
constexpr std::uint8_t kSourceLangTag = 2;
constexpr std::uint8_t kEtsSourceLang = 5;
constexpr std::size_t kSuperclassWidth = 4;
constexpr std::size_t kInterfaceIndexWidth = 2;

std::uint32_t U32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint32_t PackedVersion(const abc::AbcVersion& version) {
  return (static_cast<std::uint32_t>(version.bytes[0]) << 24) |
         (static_cast<std::uint32_t>(version.bytes[1]) << 16) |
         (static_cast<std::uint32_t>(version.bytes[2]) << 8) |
         version.bytes[3];
}

bool Uleb(std::span<const std::uint8_t> bytes, std::size_t* pos,
          std::uint32_t* value) {
  std::uint32_t result = 0;
  for (unsigned i = 0; i < 5; ++i) {
    if (*pos == bytes.size()) return false;
    const std::uint8_t byte = bytes[(*pos)++];
    if (i == 4 && (byte & 0xf0U) != 0) return false;
    result |= static_cast<std::uint32_t>(byte & 0x7fU) << (7 * i);
    if ((byte & 0x80U) == 0) {
      *value = result;
      return true;
    }
  }
  return false;
}

// Check name encoding and UTF-16 unit count before interpreting subsequent
// bytes as class metadata. MUTF-8 encodes NUL as C0 80 and supplementary
// characters as a pair of three-byte surrogate sequences.
bool ClassName(std::span<const std::uint8_t> bytes, std::size_t* pos) {
  std::uint32_t encoded_length = 0;
  if (!Uleb(bytes, pos, &encoded_length)) return false;
  const std::uint32_t expected = encoded_length >> 1;
  const bool ascii = (encoded_length & 1U) != 0;
  std::uint32_t units = 0;
  std::size_t name_start = *pos;
  while (*pos < bytes.size() && bytes[*pos] != 0) {
    const std::uint8_t head = bytes[*pos];
    if (head < 0x80U) {
      ++*pos;
    } else if (!ascii && (head & 0xe0U) == 0xc0U &&
               bytes.size() - *pos >= 2 &&
               (bytes[*pos + 1] & 0xc0U) == 0x80U &&
               (head >= 0xc2U ||
                (head == 0xc0U && bytes[*pos + 1] == 0x80U))) {
      *pos += 2;
    } else if (!ascii && (head & 0xf0U) == 0xe0U &&
               bytes.size() - *pos >= 3 &&
               (head != 0xe0U || bytes[*pos + 1] >= 0xa0U) &&
               (bytes[*pos + 1] & 0xc0U) == 0x80U &&
               (bytes[*pos + 2] & 0xc0U) == 0x80U) {
      *pos += 3;
    } else {
      return false;
    }
    if (++units > expected) return false;
  }
  if (*pos == bytes.size() || units != expected ||
      *pos - name_start < 3 || bytes[name_start] != 'L' ||
      bytes[*pos - 1] != ';') return false;
  ++*pos;
  return true;
}

std::optional<std::uint32_t> EtsTagOffset(std::span<const std::uint8_t> bytes,
                                          std::uint32_t class_offset) {
  std::size_t pos = 0;
  if (!ClassName(bytes, &pos) || bytes.size() - pos < kSuperclassWidth)
    return std::nullopt;
  pos += kSuperclassWidth;
  std::uint32_t ignored = 0;
  for (int i = 0; i < 3; ++i) {
    if (!Uleb(bytes, &pos, &ignored)) return std::nullopt;
  }
  if (pos == bytes.size()) return std::nullopt;
  if (bytes[pos] == kInterfacesTag) {
    ++pos;
    std::uint32_t count = 0;
    if (!Uleb(bytes, &pos, &count) ||
        count > (bytes.size() - pos) / kInterfaceIndexWidth) return std::nullopt;
    pos += static_cast<std::size_t>(count) * kInterfaceIndexWidth;
  }
  if (bytes.size() - pos < 2 || bytes[pos] != kSourceLangTag ||
      bytes[pos + 1] != kEtsSourceLang) return std::nullopt;
  return class_offset + static_cast<std::uint32_t>(pos);
}

struct ProbeResult {
  std::optional<std::uint32_t> tag_offset;
  bool limit = false;
  std::optional<FileError> error;
};

ProbeResult ProbeClasses(const AbcFile& file, const abc::Header& header) {
  const auto& layout = std::get<abc::StaticLayout>(header.layout);
  if (layout.classes.count > kClassLimit) return {.tag_offset = {}, .limit = true, .error = {}};
  std::size_t consumed = 0;
  for (std::uint32_t i = 0; i < layout.classes.count; ++i) {
    std::array<std::uint8_t, 4> entry{};
    if (const auto error = file.ReadAt(layout.classes.offset + 4ULL * i, entry))
      return {.tag_offset = {}, .limit = false, .error = *error};
    consumed += entry.size();
    const std::uint32_t offset = U32(entry, 0);
    if (offset >= file.Size() ||
        (offset >= header.foreign_offset &&
         offset - header.foreign_offset < header.foreign_size)) continue;
    const std::size_t length = static_cast<std::size_t>(std::min<std::uint64_t>(
        kClassProbeBytes, file.Size() - offset));
    if (length > kTotalProbeBytes - consumed) return {.tag_offset = {}, .limit = true, .error = {}};
    std::vector<std::uint8_t> bytes(length);
    if (const auto error = file.ReadAt(offset, bytes)) return {.tag_offset = {}, .limit = false, .error = *error};
    consumed += length;
    if (const auto tag = EtsTagOffset(bytes, offset)) return {.tag_offset = tag, .limit = false, .error = {}};
  }
  return {};
}
}  // namespace

DetectResult DetectAbc(const AbcFile& file,
                       const FileFingerprint& fingerprint) {
  if (const auto error = file.CheckUnchanged()) return *error;
  Detection result;
  const auto prefix = std::span(fingerprint.prefix).first(
      std::min(fingerprint.prefix_size, fingerprint.prefix.size()));
  if (prefix.size() >= kVersionOffset + kVersionWidth) {
    abc::AbcVersion version;
    std::copy_n(prefix.begin() + kVersionOffset, kVersionWidth,
                version.bytes.begin());
    result.version = version;
  }
  const auto parsed = abc::ParseHeader(prefix, fingerprint.size);
  if (const auto* error = std::get_if<abc::HeaderError>(&parsed)) {
    result.header_error = *error;
    result.kind = *error == abc::HeaderError::kIncompatibleVersion
                      ? SampleKind::kUnknown : SampleKind::kInvalid;
    result.evidence = {EvidenceRule::kHeaderFailure, 0,
                       static_cast<std::uint64_t>(*error)};
  } else {
    const abc::Header& header = std::get<abc::Header>(parsed);
    result.version = header.version;
    result.profile = header.profile;
    if (prefix.size() >= kChecksumOffset + 4 &&
        U32(prefix, kChecksumOffset) != fingerprint.adler32) {
      result.kind = SampleKind::kInvalid;
      result.evidence = {EvidenceRule::kChecksumMismatch,
                         kChecksumOffset, fingerprint.adler32};
    } else if (header.kind == abc::AbcKind::kDynamic ||
               header.kind == abc::AbcKind::kStatic) {
      result.kind = header.kind == abc::AbcKind::kDynamic
                        ? SampleKind::kDynamic : SampleKind::kStatic;
      result.evidence = {result.kind == SampleKind::kDynamic
                             ? EvidenceRule::kDynamicHeader
                             : EvidenceRule::kStaticHeader,
                         kVersionOffset, PackedVersion(header.version)};
    } else if (header.version == kLegacyStatic) {
      const auto static_parsed = abc::ParseStaticHeader(prefix, fingerprint.size);
      if (const auto* candidate = std::get_if<abc::Header>(&static_parsed)) {
        const ProbeResult probe = ProbeClasses(file, *candidate);
        if (probe.error) return *probe.error;
        if (probe.tag_offset) {
          result.kind = SampleKind::kStatic;
          result.evidence = {EvidenceRule::kEtsClass, *probe.tag_offset,
                             kEtsSourceLang};
        } else {
          result.evidence = {probe.limit ? EvidenceRule::kProbeLimit
                                         : EvidenceRule::kNoStaticWitness,
                             0, 0};
        }
      } else {
        result.evidence = {EvidenceRule::kNoStaticWitness, 0, 0};
      }
    } else {
      result.evidence = {EvidenceRule::kUnknownVersion, kVersionOffset,
                         PackedVersion(header.version)};
    }
  }
  if (const auto error = file.CheckUnchanged()) return *error;
  return result;
}

}  // namespace abcd::corpus
