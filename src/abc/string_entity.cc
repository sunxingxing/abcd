#include "abcd/abc/string_entity.h"

#include <new>

#include "abcd/corpus/abc_file.h"
#include "metadata_cursor.h"

namespace abcd::abc {
namespace {
// MUTF-8 accepts UTF-16 surrogate units; the shared metadata validator also
// accepts canonical four-byte scalars. Never normalize either representation.
constexpr std::uint8_t kAsciiLimit = 0x80;
constexpr std::uint8_t kThreeByteLead = 0xe0;
constexpr std::uint8_t kFourByteLead = 0xf0;
constexpr std::uint8_t kTwoByteMask = 0x1f;
constexpr std::uint8_t kThreeByteMask = 0x0f;
constexpr std::uint8_t kFourByteMask = 0x07;
constexpr std::uint8_t kContinuationMask = 0x3f;
constexpr unsigned kContinuationBits = 6;
constexpr std::uint32_t kSupplementaryStart = 0x10000;
constexpr std::uint32_t kHighSurrogateStart = 0xd800;
constexpr std::uint32_t kLowSurrogateStart = 0xdc00;
constexpr unsigned kSurrogateBits = 10;
constexpr std::uint32_t kSurrogateMask = 0x3ff;

bool SupportsStrings(ProfileId profile) {
  switch (profile) {
    case ProfileId::kDynamic9:
    case ProfileId::kDynamic1102:
    case ProfileId::kDynamic1202:
    case ProfileId::kDynamic1206:
    case ProfileId::kDynamic1301:
    case ProfileId::kDynamic2400:
      return true;
    case ProfileId::kUnknown:
    case ProfileId::kStatic0107:
      return false;
  }
  return false;
}
}  // namespace

StringResult ReadStringEntity(const corpus::AbcFile& file, const Header& header,
                              EntityOffset offset, StringBudget budget) {
  if (header.kind != AbcKind::kDynamic || !SupportsStrings(header.profile) ||
      !std::holds_alternative<DynamicLayout>(header.layout))
    return MetadataError::kUnsupportedProfile;
  if (file.Size() != header.file_size || file.CheckUnchanged())
    return MetadataError::kFileChanged;
  if (offset.value < kDynamicHeaderSize || offset.value >= header.file_size)
    return MetadataError::kReference;
  try {
    MetadataCursor cursor(file, offset.value, header.file_size);
    std::uint32_t units = 0;
    std::size_t spent = 0;
    auto parsed = ReadMetadataString(&cursor, &units, &spent, budget.bytes);
    if (const auto* error = std::get_if<MetadataError>(&parsed)) return *error;
    const auto& raw = std::get<std::string>(parsed);
    const auto available_units = (budget.bytes - spent) / sizeof(char16_t);
    // Include the terminator, and retain the raw buffer until conversion ends.
    if (units >= available_units) return MetadataError::kResourceLimit;
    std::u16string value;
    value.reserve(units);
    if (value.capacity() >= available_units)
      return MetadataError::kResourceLimit;
    for (std::size_t pos = 0; pos < raw.size();) {
      const auto lead = static_cast<std::uint8_t>(raw[pos++]);
      std::uint32_t scalar = lead;
      if (lead >= kAsciiLimit) {
        const unsigned followers = lead < kThreeByteLead  ? 1
                                   : lead < kFourByteLead ? 2
                                                          : 3;
        scalar &= followers == 1   ? kTwoByteMask
                  : followers == 2 ? kThreeByteMask
                                   : kFourByteMask;
        // ReadMetadataString already checked byte sequences and unit count.
        for (unsigned i = 0; i < followers; ++i)
          scalar = (scalar << kContinuationBits) |
                   (static_cast<std::uint8_t>(raw[pos++]) & kContinuationMask);
      }
      if (scalar < kSupplementaryStart) {
        value.push_back(static_cast<char16_t>(scalar));
      } else {
        scalar -= kSupplementaryStart;
        value.push_back(static_cast<char16_t>(kHighSurrogateStart +
                                              (scalar >> kSurrogateBits)));
        value.push_back(static_cast<char16_t>(kLowSurrogateStart +
                                              (scalar & kSurrogateMask)));
      }
    }
    if (file.CheckUnchanged()) return MetadataError::kFileChanged;
    return value;
  } catch (const std::bad_alloc&) {
    return MetadataError::kResourceLimit;
  }
}

}  // namespace abcd::abc
