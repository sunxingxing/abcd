#include "abcd/abc/record_attributes.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <new>
#include <span>

#include "abcd/abc/byte_reader.h"
#include "abcd/corpus/abc_file.h"
#include "metadata_cursor.h"

namespace abcd::abc {
namespace {
// JSPandaFile::JSRecordInfo defaults for the two optional metadata pointers.
constexpr std::uint32_t kNoModule = UINT32_MAX;
constexpr std::uint32_t kNoRequestPhases = 0;
enum class RecordField {
  kOther,
  kCommonJs,
  kJson,
  kModule,
  kShared,
  kAwait,
  kPhases
};

RecordField DecodeName(std::u16string_view name) {
  // External schema boundary from JSPandaFile constants, not semantic string
  // dispatch on generated code or IR. Everything after this mapping is typed.
  if (name == u"isCommonjs") return RecordField::kCommonJs;
  if (name == u"jsonFileContent") return RecordField::kJson;
  if (name == u"moduleRecordIdx") return RecordField::kModule;
  if (name == u"isSharedModule") return RecordField::kShared;
  if (name == u"hasTopLevelAwait") return RecordField::kAwait;
  if (name == u"moduleRequestPhaseIdx") return RecordField::kPhases;
  return RecordField::kOther;
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

std::optional<AttributeError> CheckWordType(const corpus::AbcFile& file,
                                            const Header& header,
                                            const MetadataIndex& index,
                                            const FieldInfo& field) {
  const auto it = std::upper_bound(
      index.regions.begin(), index.regions.end(), field.offset.value,
      [](std::uint32_t value, const IndexRegion& region) {
        return value < region.start;
      });
  if (it == index.regions.begin()) return AttributeError::kTypeReference;
  const auto& region = *std::prev(it);
  if (field.offset.value >= region.end) return AttributeError::kTypeReference;
  const auto table = region.classes;
  constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
  if (table.offset > header.file_size ||
      table.count > (header.file_size - table.offset) / kWordBytes ||
      field.type_index.value >= table.count)
    return AttributeError::kTypeReference;
  std::array<std::uint8_t, kWordBytes> bytes{};
  if (file.ReadAt(table.offset + field.type_index.value * kWordBytes, bytes))
    return AttributeError::kTypeReference;
  ByteReader reader(bytes);
  const auto encoding = std::get<std::uint32_t>(reader.ReadU32Le());
  // types.yaml codes minus U1 code, per Type::GetFieldEncoding. These VALUE
  // encodings hold offsets to U64 storage, incompatible with GetValue<U32>.
  enum class WideType : std::uint32_t { kF64 = 8, kI64 = 9, kU64 = 10 };
  switch (static_cast<WideType>(encoding)) {
    case WideType::kF64:
    case WideType::kI64:
    case WideType::kU64:
      return AttributeError::kWideValue;
  }
  return std::nullopt;
}
}  // namespace

PackagingResult DetectFilePackaging(const corpus::AbcFile& file,
                                    const Header& header,
                                    const MetadataIndex& index,
                                    StringBudget name_budget) {
  if (header.kind != AbcKind::kDynamic || !Supported(header.profile) ||
      !std::holds_alternative<DynamicLayout>(header.layout))
    return AttributeFailure{
        AttributeError::kUnsupportedProfile, {}, std::nullopt};
  if (file.Size() != header.file_size || file.CheckUnchanged())
    return AttributeFailure{AttributeError::kFileChanged, {}, std::nullopt};
  FilePackaging packaging = FilePackaging::kBundle;
  for (const auto& field : index.fields) {
    const auto name =
        ReadStringEntity(file, header, field.name_offset, name_budget);
    if (const auto* error = std::get_if<MetadataError>(&name))
      return AttributeFailure{AttributeError::kName, field.offset, *error};
    const auto kind = DecodeName(std::get<std::u16string>(name));
    if (kind == RecordField::kCommonJs || kind == RecordField::kModule) {
      packaging = FilePackaging::kMerged;
      break;
    }
  }
  if (file.CheckUnchanged())
    return AttributeFailure{AttributeError::kFileChanged, {}, std::nullopt};
  return packaging;
}

AttributeResult ReadMergedRecordAttributes(const corpus::AbcFile& file,
                                           const Header& header,
                                           const MetadataIndex& index,
                                           const RecordInfo& record,
                                           StringBudget name_budget) {
  const auto fail = [](AttributeError kind, EntityOffset field) {
    return AttributeFailure{kind, field, std::nullopt};
  };
  if (header.kind != AbcKind::kDynamic || !Supported(header.profile) ||
      !std::holds_alternative<DynamicLayout>(header.layout))
    return fail(AttributeError::kUnsupportedProfile, record.offset);
  if (file.Size() != header.file_size || file.CheckUnchanged())
    return fail(AttributeError::kFileChanged, record.offset);
  if (record.first_field > index.fields.size() ||
      record.field_count > index.fields.size() - record.first_field)
    return fail(AttributeError::kFieldRange, record.offset);
  RecordAttributes attributes;
  for (const auto& field :
       std::span(index.fields)
           .subspan(record.first_field, record.field_count)) {
    if (field.owner_offset != record.offset)
      return fail(AttributeError::kOwnerMismatch, field.offset);
    const auto name =
        ReadStringEntity(file, header, field.name_offset, name_budget);
    if (const auto* error = std::get_if<MetadataError>(&name))
      return AttributeFailure{AttributeError::kName, field.offset, *error};
    const auto kind = DecodeName(std::get<std::u16string>(name));
    if (kind == RecordField::kOther) continue;
    if (!field.value) return fail(AttributeError::kMissingValue, field.offset);
    if (field.value->encoding == FieldValueEncoding::kWord) {
      if (const auto error = CheckWordType(file, header, index, field))
        return fail(*error, field.offset);
    }
    const auto bits = field.value->bits;
    std::optional<EntityOffset>* target = nullptr;
    switch (kind) {
      case RecordField::kOther:
        break;
      case RecordField::kCommonJs:
        attributes.common_js = bits != 0;
        break;
      case RecordField::kShared:
        attributes.shared = bits != 0;
        break;
      case RecordField::kAwait:
        attributes.top_level_await = bits != 0;
        break;
      case RecordField::kModule:
        if (bits == kNoModule) {
          attributes.module.reset();
          break;
        }
        target = &attributes.module;
        break;
      case RecordField::kJson:
        target = &attributes.json_content;
        break;
      case RecordField::kPhases:
        if (bits == kNoRequestPhases) {
          attributes.request_phases.reset();
          break;
        }
        target = &attributes.request_phases;
        break;
    }
    if (target) {
      if (bits < kDynamicHeaderSize || bits >= header.file_size)
        return fail(AttributeError::kReference, field.offset);
      *target = EntityOffset{bits};
    }
  }
  if (file.CheckUnchanged())
    return fail(AttributeError::kFileChanged, record.offset);
  return attributes;
}

AttributeResult ReadBundleRecordAttributes(const corpus::AbcFile& file,
                                           const Header& header,
                                           const MetadataIndex& index,
                                           std::string_view runtime_descriptor,
                                           StringBudget name_budget) {
  const auto fail = [](AttributeError kind, EntityOffset field) {
    return AttributeFailure{kind, field, std::nullopt};
  };
  if (header.kind != AbcKind::kDynamic || !Supported(header.profile) ||
      !std::holds_alternative<DynamicLayout>(header.layout))
    return fail(AttributeError::kUnsupportedProfile, {});
  if (file.Size() != header.file_size || file.CheckUnchanged())
    return fail(AttributeError::kFileChanged, {});
  RecordAttributes attributes;
  attributes.common_js = false;
  attributes.shared = false;
  attributes.top_level_await = false;
  for (const auto& record : index.records) {
    // External class-name schema of InitializeUnMergedPF. Field enumeration
    // does not short-circuit: the last non-excluded value in this class wins.
    if (!attributes.module && record.descriptor == "L_ESModuleRecord;") {
      if (record.first_field > index.fields.size() ||
          record.field_count > index.fields.size() - record.first_field)
        return fail(AttributeError::kFieldRange, record.offset);
      for (const auto& field :
           std::span(index.fields)
               .subspan(record.first_field, record.field_count)) {
        if (field.owner_offset != record.offset)
          return fail(AttributeError::kOwnerMismatch, field.offset);
        MetadataCursor cursor(file, field.name_offset.value, header.file_size);
        std::uint32_t units = 0;
        std::size_t spent = 0;
        const auto name = [&]() -> std::variant<std::string, MetadataError> {
          try {
            return ReadMetadataString(&cursor, &units, &spent, name_budget.bytes);
          } catch (const std::bad_alloc&) {
            return MetadataError::kResourceLimit;
          }
        }();
        if (const auto* error = std::get_if<MetadataError>(&name))
          return AttributeFailure{AttributeError::kName, field.offset, *error};
        // Pinned VM compares a MUTF-8 byte view whose length is utf16_length.
        // Do not normalize or replace this with decoded Unicode comparison.
        if (std::string_view(std::get<std::string>(name)).substr(0, units) ==
            runtime_descriptor)
          continue;
        if (!field.value)
          return fail(AttributeError::kMissingValue, field.offset);
        if (field.value->encoding == FieldValueEncoding::kWord) {
          if (const auto error = CheckWordType(file, header, index, field))
            return fail(*error, field.offset);
        }
        const auto bits = field.value->bits;
        if (bits == kNoModule) {
          attributes.module.reset();
        } else {
          if (bits < kDynamicHeaderSize || bits >= header.file_size)
            return fail(AttributeError::kReference, field.offset);
          attributes.module = EntityOffset{bits};
        }
      }
    }
    if (record.descriptor == "L_CommonJsRecord;") attributes.common_js = true;
    if (record.descriptor == "isSharedModule") attributes.shared = true;
    if (record.descriptor == "L_HasTopLevelAwait;")
      attributes.top_level_await = true;
  }
  if (file.CheckUnchanged()) return fail(AttributeError::kFileChanged, {});
  return attributes;
}

}  // namespace abcd::abc
