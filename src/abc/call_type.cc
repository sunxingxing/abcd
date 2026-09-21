#include "abcd/abc/call_type.h"

#include <algorithm>
#include <iterator>
#include <new>
#include <string_view>

#include "abcd/corpus/abc_file.h"
#include "metadata_cursor.h"

namespace abcd::abc {
namespace {
// MethodLiteral::Initialize and AnnotationDataAccessor at the pinned 7.0 tag.
constexpr std::uint8_t kAnnotationTag = 6;
constexpr std::uint32_t kReferenceBytes = sizeof(std::uint32_t);
constexpr std::uint32_t kTaggedReferenceBytes = 1 + kReferenceBytes;
constexpr std::uint32_t kElementBytes = 2 * kReferenceBytes;
constexpr std::uint32_t kElementTagBytes = 1;
constexpr std::string_view kDescriptor = "L_ESCallTypeAnnotation;";
constexpr std::string_view kElementName = "callType";

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

std::variant<bool, MetadataError> MatchName(const corpus::AbcFile& file,
                                            const Header& header,
                                            std::uint32_t offset,
                                            std::string_view expected,
                                            MetadataBudget budget) {
  if (offset < kDynamicHeaderSize || offset >= header.file_size)
    return MetadataError::kReference;
  MetadataCursor cursor(file, offset, header.file_size);
  std::uint32_t units = 0;
  std::size_t spent = 0;
  auto result = ReadMetadataString(&cursor, &units, &spent, budget.bytes);
  if (const auto* error = std::get_if<MetadataError>(&result)) return *error;
  const auto& bytes = std::get<std::string>(result);
  // Reproduce runtime's raw-byte view with UTF-16 length, not Unicode-name
  // normalization. Valid MUTF-8 has at least as many bytes as UTF-16 units.
  if (units > bytes.size()) return MetadataError::kBadString;
  return std::string_view(bytes.data(), units) == expected;
}

std::variant<CallType, MetadataError> ReadAnnotation(
    const corpus::AbcFile& file, const Header& header,
    const MetadataIndex& index, std::uint32_t offset, MetadataBudget budget) {
  if (offset < kDynamicHeaderSize || offset >= header.file_size)
    return MetadataError::kReference;
  MetadataCursor cursor(file, offset, header.file_size);
  const auto class_index = cursor.U16();
  if (const auto* error = std::get_if<MetadataError>(&class_index))
    return *error;
  const auto count = cursor.U16();
  if (const auto* error = std::get_if<MetadataError>(&count)) return *error;
  const auto elements = std::get<std::uint16_t>(count);
  if (elements > (header.file_size - cursor.Position()) /
                     (kElementBytes + kElementTagBytes))
    return MetadataError::kTruncated;

  const auto it =
      std::upper_bound(index.regions.begin(), index.regions.end(), offset,
                       [](std::uint32_t value, const IndexRegion& region) {
                         return value < region.start;
                       });
  if (it == index.regions.begin()) return MetadataError::kRegionRange;
  const auto& region = *std::prev(it);
  if (offset >= region.end) return MetadataError::kRegionRange;
  const auto table = region.classes;
  const auto slot = std::get<std::uint16_t>(class_index);
  if (table.offset > header.file_size ||
      table.count > (header.file_size - table.offset) / kReferenceBytes ||
      slot >= table.count)
    return MetadataError::kIndexRange;
  MetadataCursor class_cursor(file, table.offset + slot * kReferenceBytes,
                              header.file_size);
  const auto descriptor = class_cursor.U32();
  if (const auto* error = std::get_if<MetadataError>(&descriptor))
    return *error;
  const auto match = MatchName(
      file, header, std::get<std::uint32_t>(descriptor), kDescriptor, budget);
  if (const auto* error = std::get_if<MetadataError>(&match)) return *error;
  if (!std::get<bool>(match)) return CallType{};
  for (std::uint32_t i = 0; i < elements; ++i) {
    const auto name = cursor.U32();
    if (const auto* error = std::get_if<MetadataError>(&name)) return *error;
    const auto value = cursor.U32();
    if (const auto* error = std::get_if<MetadataError>(&value)) return *error;
    const auto element_match = MatchName(
        file, header, std::get<std::uint32_t>(name), kElementName, budget);
    if (const auto* error = std::get_if<MetadataError>(&element_match))
      return *error;
    // The runtime ignores element type tags and stops at the first matching
    // name in each annotation. Do not let later duplicate elements override.
    if (std::get<bool>(element_match))
      return CallType{std::get<std::uint32_t>(value)};
  }
  return CallType{};
}
}  // namespace

std::variant<CallType, MetadataError> ReadCallType(
    const corpus::AbcFile& file, const Header& header,
    const MetadataIndex& index, MethodAnnotations annotations,
    MetadataBudget budget) {
  if (header.kind != AbcKind::kDynamic ||
      !std::holds_alternative<DynamicLayout>(header.layout) ||
      !Supported(header.profile))
    return MetadataError::kUnsupportedProfile;
  if (file.Size() != header.file_size || file.CheckUnchanged())
    return MetadataError::kFileChanged;
  if (annotations.count != 0 &&
      (annotations.first_tag.value < kDynamicHeaderSize ||
       annotations.first_tag.value > header.file_size ||
       annotations.count > (header.file_size - annotations.first_tag.value) /
                               kTaggedReferenceBytes))
    return MetadataError::kReference;
  try {
    CallType result;
    MetadataCursor cursor(file, annotations.first_tag.value, header.file_size);
    for (std::uint32_t i = 0; i < annotations.count; ++i) {
      const auto tag = cursor.U8();
      if (const auto* error = std::get_if<MetadataError>(&tag)) return *error;
      if (std::get<std::uint8_t>(tag) != kAnnotationTag)
        return MetadataError::kUnknownTag;
      const auto reference = cursor.U32();
      if (const auto* error = std::get_if<MetadataError>(&reference))
        return *error;
      const auto annotation = ReadAnnotation(
          file, header, index, std::get<std::uint32_t>(reference), budget);
      if (const auto* error = std::get_if<MetadataError>(&annotation))
        return *error;
      if (std::get<CallType>(annotation).value)
        result = std::get<CallType>(annotation);
    }
    if (file.CheckUnchanged()) return MetadataError::kFileChanged;
    return result;
  } catch (const std::bad_alloc&) {
    return MetadataError::kResourceLimit;
  }
}
}  // namespace abcd::abc
