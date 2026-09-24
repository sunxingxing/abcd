#include "abcd/abc/metadata_index.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "abcd/corpus/abc_file.h"
#include "metadata_cursor.h"

namespace abcd::abc {
namespace {

// Pinned runtime_core 3e147bb: libpandafile/file.h and file_items.h.
constexpr std::uint32_t kIndexWordBytes = 4;
constexpr std::uint32_t kRegionBytes = 40;
constexpr std::uint32_t kLocalFieldHeaderBytes = 8;
constexpr std::uint32_t kLocalMethodHeaderBytes = 8;
constexpr std::uint16_t kNoProtoIndex = UINT16_MAX;

enum class ClassTag : std::uint8_t {
  kEnd = 0,
  kInterfaces = 1,
  kSourceLanguage = 2,
  kRuntimeAnnotation = 3,
  kAnnotation = 4,
  kRuntimeTypeAnnotation = 5,
  kTypeAnnotation = 6,
  kSourceFile = 7,
};
enum class FieldTag : std::uint8_t {
  kEnd = 0,
  kIntValue = 1,
  kValue = 2,
  kRuntimeAnnotation = 3,
  kAnnotation = 4,
  kRuntimeTypeAnnotation = 5,
  kTypeAnnotation = 6,
};
enum class MethodTag : std::uint8_t {
  kEnd = 0,
  kCode = 1,
  kSourceLanguage = 2,
  kRuntimeAnnotation = 3,
  kRuntimeParamAnnotation = 4,
  kDebugInfo = 5,
  kAnnotation = 6,
  kParamAnnotation = 7,
  kTypeAnnotation = 8,
  kRuntimeTypeAnnotation = 9,
};

using Region = IndexRegion;

template <typename T>
bool Take(const std::variant<T, MetadataError>& result, T* value,
          MetadataError* error) {
  if (const auto* failure = std::get_if<MetadataError>(&result)) {
    *error = *failure;
    return false;
  }
  *value = std::get<T>(result);
  return true;
}

bool InForeign(const Header& header, std::uint32_t offset) {
  return offset >= header.foreign_offset &&
         offset - header.foreign_offset < header.foreign_size;
}

bool ValidEntity(const Header& header, std::uint32_t offset) {
  return offset >= kDynamicHeaderSize && offset < header.file_size;
}

bool IsAbsentTable(IndexSection table) {
  return table.count == UINT32_MAX && table.offset == UINT32_MAX;
}

bool ValidTable(IndexSection table, std::uint32_t file_size) {
  return table.offset <= file_size &&
         table.count <= (file_size - table.offset) / kIndexWordBytes;
}

std::variant<std::uint32_t, MetadataError> TableEntry(
    const corpus::AbcFile& file, const Header& header, IndexSection table,
    std::uint32_t index) {
  if (index >= table.count || !ValidTable(table, header.file_size))
    return MetadataError::kIndexRange;
  if (IsAbsentTable(table)) return MetadataError::kIndexRange;
  MetadataCursor cursor(file, table.offset + index * kIndexWordBytes,
                        header.file_size);
  return cursor.U32();
}

std::variant<std::vector<Region>, MetadataError> ReadRegions(
    const corpus::AbcFile& file, const Header& header, IndexSection section,
    std::size_t* spent, std::size_t budget) {
  if (section.offset > header.file_size ||
      section.count > (header.file_size - section.offset) / kRegionBytes)
    return MetadataError::kIndexRange;
  if (section.count > (budget - *spent) / sizeof(Region))
    return MetadataError::kResourceLimit;
  std::vector<Region> regions;
  regions.reserve(section.count);
  *spent += section.count * sizeof(Region);
  MetadataCursor cursor(file, section.offset, header.file_size);
  for (std::uint32_t i = 0; i < section.count; ++i) {
    Region region;
    MetadataError error{};
    if (!Take(cursor.U32(), &region.start, &error) ||
        !Take(cursor.U32(), &region.end, &error) ||
        !Take(cursor.U32(), &region.classes.count, &error) ||
        !Take(cursor.U32(), &region.classes.offset, &error) ||
        !Take(cursor.U32(), &region.methods.count, &error) ||
        !Take(cursor.U32(), &region.methods.offset, &error) ||
        !Take(cursor.U32(), &region.fields.count, &error) ||
        !Take(cursor.U32(), &region.fields.offset, &error) ||
        !Take(cursor.U32(), &region.protos.count, &error) ||
        !Take(cursor.U32(), &region.protos.offset, &error))
      return error;
    if (region.start < kDynamicHeaderSize || region.start >= region.end ||
        region.end > header.file_size)
      return MetadataError::kRegionRange;
    const bool can_omit = header.profile != ProfileId::kDynamic9 &&
                          header.profile != ProfileId::kDynamic1102;
    if (!ValidTable(region.classes, header.file_size) ||
        !ValidTable(region.methods, header.file_size) ||
        (!ValidTable(region.fields, header.file_size) &&
         !(can_omit && IsAbsentTable(region.fields))) ||
        (!ValidTable(region.protos, header.file_size) &&
         !(can_omit && IsAbsentTable(region.protos))))
      return MetadataError::kIndexRange;
    regions.push_back(region);
  }
  std::sort(regions.begin(), regions.end(),
            [](const Region& a, const Region& b) { return a.start < b.start; });
  for (std::size_t i = 1; i < regions.size(); ++i) {
    if (regions[i - 1].end > regions[i].start)
      return MetadataError::kRegionOverlap;
  }
  return regions;
}

const Region* FindRegion(const std::vector<Region>& regions,
                         std::uint32_t offset) {
  const auto it =
      std::upper_bound(regions.begin(), regions.end(), offset,
                       [](std::uint32_t value, const Region& region) {
                         return value < region.start;
                       });
  if (it == regions.begin()) return nullptr;
  const Region& prior = *std::prev(it);
  return offset < prior.end ? &prior : nullptr;
}

std::variant<std::uint32_t, MetadataError> Resolve(
    const corpus::AbcFile& file, const Header& header,
    const std::vector<Region>& regions, std::uint32_t entity,
    std::uint16_t index, bool proto) {
  const Region* region = FindRegion(regions, entity);
  if (!region) return MetadataError::kRegionRange;
  const auto offset =
      TableEntry(file, header, proto ? region->protos : region->classes, index);
  if (std::holds_alternative<MetadataError>(offset))
    return std::get<MetadataError>(offset);
  if (!ValidEntity(header, std::get<std::uint32_t>(offset)))
    return MetadataError::kReference;
  return std::get<std::uint32_t>(offset);
}

std::optional<MetadataError> ReadClassTags(MetadataCursor* cursor,
                                           const Header& header,
                                           RecordInfo& record) {
  bool seen_interfaces = false;
  bool seen_language = false;
  bool seen_source_file = false;
  std::uint8_t last_tag = 0;
  for (;;) {
    const auto raw = cursor->U8();
    if (std::holds_alternative<MetadataError>(raw))
      return std::get<MetadataError>(raw);
    const auto tag = std::get<std::uint8_t>(raw);
    if (tag != 0 && tag < last_tag) return MetadataError::kTagOrder;
    if (tag != 0) last_tag = tag;
    switch (static_cast<ClassTag>(tag)) {
      case ClassTag::kEnd:
        return std::nullopt;
      case ClassTag::kInterfaces: {
        if (seen_interfaces) return MetadataError::kDuplicateTag;
        seen_interfaces = true;
        const auto count = cursor->Uleb();
        if (std::holds_alternative<MetadataError>(count))
          return std::get<MetadataError>(count);
        if (!cursor->Skip(
                static_cast<std::uint64_t>(std::get<std::uint32_t>(count)) * 2))
          return MetadataError::kTruncated;
        break;
      }
      case ClassTag::kSourceLanguage:
        if (seen_language) return MetadataError::kDuplicateTag;
        seen_language = true;
        if (std::holds_alternative<MetadataError>(cursor->U8()))
          return MetadataError::kTruncated;
        break;
      case ClassTag::kRuntimeAnnotation:
      case ClassTag::kAnnotation:
      case ClassTag::kRuntimeTypeAnnotation:
      case ClassTag::kTypeAnnotation:
      case ClassTag::kSourceFile: {
        if (static_cast<ClassTag>(std::get<std::uint8_t>(raw)) ==
            ClassTag::kSourceFile) {
          if (seen_source_file) return MetadataError::kDuplicateTag;
          seen_source_file = true;
        }
        const auto ref = cursor->U32();
        if (std::holds_alternative<MetadataError>(ref))
          return std::get<MetadataError>(ref);
        if (!ValidEntity(header, std::get<std::uint32_t>(ref)))
          return MetadataError::kReference;
        if (static_cast<ClassTag>(tag) == ClassTag::kSourceFile)
          record.source_file = EntityOffset{std::get<std::uint32_t>(ref)};
        break;
      }
      default:
        return MetadataError::kUnknownTag;
    }
  }
}

std::optional<MetadataError> ReadField(MetadataCursor* cursor,
                                       const Header& header,
                                       const corpus::AbcFile& file,
                                       const std::vector<Region>& regions,
                                       std::uint32_t owner, FieldInfo* info) {
  const std::uint32_t field = cursor->Position();
  std::uint16_t class_index = 0, type_index = 0;
  std::uint32_t name = 0, flags = 0;
  MetadataError error{};
  if (!Take(cursor->U16(), &class_index, &error) ||
      !Take(cursor->U16(), &type_index, &error) ||
      !Take(cursor->U32(), &name, &error) ||
      !Take(cursor->Uleb(), &flags, &error))
    return error;
  info->offset.value = field;
  info->owner_offset.value = owner;
  info->name_offset.value = name;
  info->type_index.value = type_index;
  info->access_flags = flags;
  const auto resolved =
      Resolve(file, header, regions, field, class_index, false);
  if (std::holds_alternative<MetadataError>(resolved))
    return std::get<MetadataError>(resolved);
  if (std::get<std::uint32_t>(resolved) != owner)
    return MetadataError::kOwnerMismatch;
  if (!ValidEntity(header, name)) return MetadataError::kReference;
  bool seen_value = false;
  std::uint8_t last_tag = 0;
  for (;;) {
    const auto raw = cursor->U8();
    if (std::holds_alternative<MetadataError>(raw))
      return std::get<MetadataError>(raw);
    const auto tag = std::get<std::uint8_t>(raw);
    if (tag != 0 && tag < last_tag) return MetadataError::kTagOrder;
    if (tag != 0) last_tag = tag;
    switch (static_cast<FieldTag>(tag)) {
      case FieldTag::kEnd:
        return std::nullopt;
      case FieldTag::kIntValue:
        if (seen_value) return MetadataError::kDuplicateTag;
        seen_value = true;
        {
          const auto value = cursor->SlebBits();
          if (const auto* failure = std::get_if<MetadataError>(&value))
            return *failure;
          info->value = FieldValue{FieldValueEncoding::kSignedLeb,
                                   std::get<std::uint32_t>(value)};
        }
        break;
      case FieldTag::kValue:
      case FieldTag::kRuntimeAnnotation:
      case FieldTag::kAnnotation:
      case FieldTag::kRuntimeTypeAnnotation:
      case FieldTag::kTypeAnnotation: {
        if (static_cast<FieldTag>(std::get<std::uint8_t>(raw)) ==
            FieldTag::kValue) {
          if (seen_value) return MetadataError::kDuplicateTag;
          seen_value = true;
        }
        const auto value = cursor->U32();
        if (std::holds_alternative<MetadataError>(value))
          return std::get<MetadataError>(value);
        if (static_cast<FieldTag>(tag) == FieldTag::kValue)
          info->value = FieldValue{FieldValueEncoding::kWord,
                                   std::get<std::uint32_t>(value)};
        if (static_cast<FieldTag>(std::get<std::uint8_t>(raw)) !=
                FieldTag::kValue &&
            !ValidEntity(header, std::get<std::uint32_t>(value)))
          return MetadataError::kReference;
        break;
      }
      default:
        return MetadataError::kUnknownTag;
    }
  }
}

std::variant<CodeInfo, MetadataError> ReadCode(const corpus::AbcFile& file,
                                               const Header& header,
                                               std::uint32_t offset) {
  if (!ValidEntity(header, offset)) return MetadataError::kReference;
  MetadataCursor cursor(file, offset, header.file_size);
  CodeInfo code;
  code.offset.value = offset;
  MetadataError error{};
  if (!Take(cursor.Uleb(), &code.registers, &error) ||
      !Take(cursor.Uleb(), &code.arguments, &error) ||
      !Take(cursor.Uleb(), &code.instruction_bytes, &error) ||
      !Take(cursor.Uleb(), &code.try_count, &error))
    return error;
  code.instruction_offset.value = cursor.Position();
  if (!cursor.Skip(code.instruction_bytes)) return MetadataError::kCodeRange;
  return code;
}

std::variant<MethodInfo, MetadataError> ReadMethod(
    MetadataCursor* cursor, const Header& header, const corpus::AbcFile& file,
    const std::vector<Region>& regions, std::uint32_t owner, std::size_t* spent,
    std::size_t budget) {
  MethodInfo method;
  method.offset.value = cursor->Position();
  std::uint16_t class_index = 0, proto_index = 0;
  std::uint32_t name_offset = 0;
  MetadataError error{};
  if (!Take(cursor->U16(), &class_index, &error) ||
      !Take(cursor->U16(), &proto_index, &error) ||
      !Take(cursor->U32(), &name_offset, &error) ||
      !Take(cursor->Uleb(), &method.access_flags, &error))
    return error;
  const auto class_offset =
      Resolve(file, header, regions, method.offset.value, class_index, false);
  if (std::holds_alternative<MetadataError>(class_offset))
    return std::get<MetadataError>(class_offset);
  method.owner_offset.value = std::get<std::uint32_t>(class_offset);
  if (method.owner_offset.value != owner) return MetadataError::kOwnerMismatch;
  if (proto_index != kNoProtoIndex) {
    const auto proto =
        Resolve(file, header, regions, method.offset.value, proto_index, true);
    if (std::holds_alternative<MetadataError>(proto))
      return std::get<MetadataError>(proto);
    method.proto_offset = EntityOffset{std::get<std::uint32_t>(proto)};
  }
  if (!ValidEntity(header, name_offset)) return MetadataError::kReference;
  method.name_offset = {name_offset};
  MetadataCursor name_reader(file, name_offset, header.file_size);
  auto name = ReadMetadataString(&name_reader, &method.name_utf16_length, spent,
                                 budget);
  if (std::holds_alternative<MetadataError>(name))
    return std::get<MetadataError>(name);
  method.name = std::move(std::get<std::string>(name));
  bool seen_code = false;
  bool seen_language = false;
  bool seen_debug = false;
  std::uint8_t last_tag = 0;
  for (;;) {
    const auto raw = cursor->U8();
    if (std::holds_alternative<MetadataError>(raw))
      return std::get<MetadataError>(raw);
    const auto tag = std::get<std::uint8_t>(raw);
    if (tag != 0 && tag < last_tag) return MetadataError::kTagOrder;
    if (tag != 0) last_tag = tag;
    switch (static_cast<MethodTag>(tag)) {
      case MethodTag::kEnd:
        return method;
      case MethodTag::kCode: {
        if (seen_code) return MetadataError::kDuplicateTag;
        seen_code = true;
        std::uint32_t code_offset = 0;
        if (!Take(cursor->U32(), &code_offset, &error)) return error;
        const auto code = ReadCode(file, header, code_offset);
        if (std::holds_alternative<MetadataError>(code))
          return std::get<MetadataError>(code);
        method.code = std::get<CodeInfo>(code);
        break;
      }
      case MethodTag::kSourceLanguage:
        if (seen_language) return MetadataError::kDuplicateTag;
        seen_language = true;
        if (std::holds_alternative<MetadataError>(cursor->U8()))
          return MetadataError::kTruncated;
        break;
      case MethodTag::kRuntimeAnnotation:
      case MethodTag::kRuntimeParamAnnotation:
      case MethodTag::kDebugInfo:
      case MethodTag::kAnnotation:
      case MethodTag::kParamAnnotation:
      case MethodTag::kTypeAnnotation:
      case MethodTag::kRuntimeTypeAnnotation: {
        if (static_cast<MethodTag>(std::get<std::uint8_t>(raw)) ==
            MethodTag::kDebugInfo) {
          if (seen_debug) return MetadataError::kDuplicateTag;
          seen_debug = true;
        }
        const auto ref = cursor->U32();
        if (std::holds_alternative<MetadataError>(ref))
          return std::get<MetadataError>(ref);
        if (!ValidEntity(header, std::get<std::uint32_t>(ref)))
          return MetadataError::kReference;
        if (static_cast<MethodTag>(tag) == MethodTag::kDebugInfo)
          method.debug_info = EntityOffset{std::get<std::uint32_t>(ref)};
        if (static_cast<MethodTag>(tag) == MethodTag::kAnnotation) {
          constexpr std::uint32_t kTaggedReferenceBytes =
              sizeof(std::uint8_t) + sizeof(std::uint32_t);
          if (method.annotations.count == 0)
            method.annotations.first_tag =
                EntityOffset{cursor->Position() - kTaggedReferenceBytes};
          ++method.annotations.count;
        }
        break;
      }
      default:
        return MetadataError::kUnknownTag;
    }
  }
}

}  // namespace

MetadataResult BuildMetadataIndex(const corpus::AbcFile& file,
                                  const Header& header, MetadataBudget budget) {
  if (header.kind != AbcKind::kDynamic ||
      !std::holds_alternative<DynamicLayout>(header.layout) ||
      header.profile == ProfileId::kUnknown)
    return MetadataError::kUnsupportedProfile;
  if (header.file_size != file.Size() || file.CheckUnchanged())
    return MetadataError::kFileChanged;
  const DynamicLayout layout = std::get<DynamicLayout>(header.layout);
  if (!ValidTable(layout.classes, header.file_size))
    return MetadataError::kIndexRange;
  std::size_t spent = 0;
  auto region_result =
      ReadRegions(file, header, layout.index_regions, &spent, budget.bytes);
  if (std::holds_alternative<MetadataError>(region_result))
    return std::get<MetadataError>(region_result);
  const auto& regions = std::get<std::vector<Region>>(region_result);
  if (layout.classes.count > (budget.bytes - spent) / sizeof(RecordInfo))
    return MetadataError::kResourceLimit;
  const std::size_t record_bytes =
      static_cast<std::size_t>(layout.classes.count) * sizeof(RecordInfo);
  spent += record_bytes;
  if (layout.classes.count > (budget.bytes - spent) / sizeof(std::uint32_t))
    return MetadataError::kResourceLimit;
  std::vector<std::uint32_t> class_offsets;
  class_offsets.reserve(layout.classes.count);
  spent +=
      static_cast<std::size_t>(layout.classes.count) * sizeof(std::uint32_t);
  for (std::uint32_t i = 0; i < layout.classes.count; ++i) {
    const auto class_entry = TableEntry(file, header, layout.classes, i);
    if (std::holds_alternative<MetadataError>(class_entry))
      return std::get<MetadataError>(class_entry);
    const auto offset = std::get<std::uint32_t>(class_entry);
    if (!ValidEntity(header, offset)) return MetadataError::kReference;
    class_offsets.push_back(offset);
  }
  std::sort(class_offsets.begin(), class_offsets.end());
  if (std::adjacent_find(class_offsets.begin(), class_offsets.end()) !=
      class_offsets.end())
    return MetadataError::kIndexRange;
  MetadataIndex index;
  index.records.reserve(layout.classes.count);
  for (const std::uint32_t offset : class_offsets) {
    if (InForeign(header, offset)) continue;
    if (!FindRegion(regions, offset)) return MetadataError::kRegionRange;
    MetadataCursor cursor(file, offset, header.file_size);
    RecordInfo record;
    record.offset.value = offset;
    record.first_method = index.methods.size();
    record.first_field = index.fields.size();
    auto descriptor =
        ReadMetadataString(&cursor, &record.utf16_length, &spent, budget.bytes);
    if (std::holds_alternative<MetadataError>(descriptor))
      return std::get<MetadataError>(descriptor);
    record.descriptor = std::move(std::get<std::string>(descriptor));
    if (record.descriptor.size() < 3 || record.descriptor.front() != 'L' ||
        record.descriptor.back() != ';')
      return MetadataError::kBadString;
    std::uint32_t super = 0;
    MetadataError error{};
    if (!Take(cursor.U32(), &super, &error) ||
        !Take(cursor.Uleb(), &record.access_flags, &error) ||
        !Take(cursor.Uleb(), &record.field_count, &error) ||
        !Take(cursor.Uleb(), &record.method_count, &error))
      return error;
    if (super && !ValidEntity(header, super)) return MetadataError::kReference;
    if (const auto failure = ReadClassTags(&cursor, header, record)) return *failure;
    const auto remaining = header.file_size - cursor.Position();
    if (record.field_count > remaining / kLocalFieldHeaderBytes ||
        record.method_count > remaining / kLocalMethodHeaderBytes)
      return MetadataError::kTruncated;
    for (std::uint32_t field = 0; field < record.field_count; ++field) {
      if (index.fields.size() == index.fields.capacity()) {
        const auto old_capacity = index.fields.capacity();
        if (old_capacity > std::numeric_limits<std::size_t>::max() / 2)
          return MetadataError::kResourceLimit;
        const auto requested = old_capacity == 0 ? 1 : old_capacity * 2;
        // Admit both old and new allocations while growing the retained index.
        if (requested > (budget.bytes - spent) / sizeof(FieldInfo))
          return MetadataError::kResourceLimit;
        index.fields.reserve(requested);
        if (index.fields.capacity() >
            (budget.bytes - spent) / sizeof(FieldInfo))
          return MetadataError::kResourceLimit;
        spent += (index.fields.capacity() - old_capacity) * sizeof(FieldInfo);
      }
      FieldInfo info;
      if (const auto failure =
              ReadField(&cursor, header, file, regions, offset, &info))
        return *failure;
      index.fields.push_back(info);
    }
    for (std::uint32_t method = 0; method < record.method_count; ++method) {
      if (index.methods.size() == index.methods.capacity()) {
        const std::size_t old_capacity = index.methods.capacity();
        const std::size_t new_capacity =
            old_capacity == 0 ? 1
            : old_capacity > std::numeric_limits<std::size_t>::max() / 2
                ? std::numeric_limits<std::size_t>::max()
                : old_capacity * 2;
        const std::size_t added = new_capacity - old_capacity;
        // Reallocation temporarily holds both buffers.
        if (new_capacity > (budget.bytes - spent) / sizeof(MethodInfo))
          return MetadataError::kResourceLimit;
        index.methods.reserve(new_capacity);
        spent += added * sizeof(MethodInfo);
      }
      auto parsed = ReadMethod(&cursor, header, file, regions, offset, &spent,
                               budget.bytes);
      if (std::holds_alternative<MetadataError>(parsed))
        return std::get<MetadataError>(parsed);
      index.methods.push_back(std::move(std::get<MethodInfo>(parsed)));
    }
    index.records.push_back(std::move(record));
  }
  if (file.CheckUnchanged()) return MetadataError::kFileChanged;
  index.regions = std::move(std::get<std::vector<Region>>(region_result));
  return index;
}

}  // namespace abcd::abc
