#include "abcd/abc/literal_array.h"

#include <new>

#include "abcd/corpus/abc_file.h"
#include "metadata_cursor.h"

namespace abcd::abc {
namespace {
constexpr std::uint32_t kCountUnitsPerEntry = 2;
constexpr std::uint32_t kMinimumEntryBytes = 2;
constexpr std::uint8_t kTagValue = 0x00;
constexpr std::uint8_t kFirstPackedTag = 0x0a;
constexpr std::uint8_t kLastPackedTag = 0x15;
constexpr unsigned kByteBits = 8;

struct PayloadLayout {
  std::uint8_t bytes;
  bool reference;
};

std::variant<PayloadLayout, LiteralError> Layout(std::uint8_t raw) {
  if (raw == kTagValue) return LiteralError::kUnsupportedTag;
  if (raw >= kFirstPackedTag && raw <= kLastPackedTag)
    return LiteralError::kPackedArray;
  switch (static_cast<LiteralTag>(raw)) {
    case LiteralTag::kBoolean:
    case LiteralTag::kAccessor:
    case LiteralTag::kBuiltinTypeIndex:
    case LiteralTag::kNull:
      return PayloadLayout{sizeof(std::uint8_t), false};
    case LiteralTag::kMethodAffiliate:
      return PayloadLayout{sizeof(std::uint16_t), false};
    case LiteralTag::kInteger:
    case LiteralTag::kFloat:
    case LiteralTag::kLiteralBufferIndex:
      return PayloadLayout{sizeof(std::uint32_t), false};
    case LiteralTag::kDouble:
      return PayloadLayout{sizeof(std::uint64_t), false};
    case LiteralTag::kString:
    case LiteralTag::kMethod:
    case LiteralTag::kGeneratorMethod:
    case LiteralTag::kAsyncGeneratorMethod:
    case LiteralTag::kLiteralArray:
    case LiteralTag::kGetter:
    case LiteralTag::kSetter:
    case LiteralTag::kImplements:
      return PayloadLayout{sizeof(std::uint32_t), true};
  }
  return LiteralError::kUnknownTag;
}

bool Supported(ProfileId profile) {
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
LiteralError ReadError(MetadataError error) {
  return error == MetadataError::kRead ? LiteralError::kRead
                                       : LiteralError::kTruncated;
}
}  // namespace

LiteralResult ReadLiteralArray(const corpus::AbcFile& file,
                               const Header& header, EntityOffset offset,
                               LiteralBudget budget) {
  const auto fail = [&](LiteralError kind) {
    return LiteralFailure{kind, offset};
  };
  if (header.kind != AbcKind::kDynamic || !Supported(header.profile) ||
      !std::holds_alternative<DynamicLayout>(header.layout))
    return fail(LiteralError::kUnsupportedProfile);
  if (header.file_size != file.Size() || file.CheckUnchanged())
    return fail(LiteralError::kFileChanged);
  if (offset.value < kDynamicHeaderSize || offset.value >= header.file_size)
    return fail(LiteralError::kReference);
  try {
    MetadataCursor cursor(file, offset.value, header.file_size);
    const auto count_result = cursor.U32();
    if (const auto* error = std::get_if<MetadataError>(&count_result))
      return fail(ReadError(*error));
    const auto count = std::get<std::uint32_t>(count_result);
    if (count % kCountUnitsPerEntry != 0) return fail(LiteralError::kOddCount);
    const auto entries = count / kCountUnitsPerEntry;
    if (entries > (header.file_size - cursor.Position()) / kMinimumEntryBytes)
      return fail(LiteralError::kTruncated);
    if (entries > budget.bytes / sizeof(LiteralEntry))
      return fail(LiteralError::kResourceLimit);
    LiteralArray array;
    array.entries.reserve(entries);
    if (array.entries.capacity() > budget.bytes / sizeof(LiteralEntry))
      return fail(LiteralError::kResourceLimit);
    for (std::uint32_t i = 0; i < entries; ++i) {
      offset.value = cursor.Position();
      const auto tag_result = cursor.U8();
      if (const auto* error = std::get_if<MetadataError>(&tag_result))
        return fail(ReadError(*error));
      const auto raw = std::get<std::uint8_t>(tag_result);
      const auto layout_result = Layout(raw);
      if (const auto* error = std::get_if<LiteralError>(&layout_result))
        return fail(*error);
      const auto layout = std::get<PayloadLayout>(layout_result);
      std::uint64_t bits = 0;
      for (unsigned part = 0; part < layout.bytes; ++part) {
        const auto next = cursor.U8();
        if (const auto* error = std::get_if<MetadataError>(&next))
          return fail(ReadError(*error));
        bits |= static_cast<std::uint64_t>(std::get<std::uint8_t>(next))
                << (part * kByteBits);
      }
      LiteralEntry entry{static_cast<LiteralTag>(raw), LiteralBits{bits}};
      if (layout.reference) {
        if (bits < kDynamicHeaderSize || bits >= header.file_size)
          return fail(LiteralError::kReference);
        entry.value = EntityOffset{static_cast<std::uint32_t>(bits)};
      }
      array.entries.push_back(entry);
    }
    array.end.value = cursor.Position();
    if (file.CheckUnchanged()) return fail(LiteralError::kFileChanged);
    return array;
  } catch (const std::bad_alloc&) {
    return fail(LiteralError::kResourceLimit);
  }
}

}  // namespace abcd::abc
