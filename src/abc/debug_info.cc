#include "abcd/abc/debug_info.h"

#include <new>
#include <stdexcept>

#include "abcd/corpus/abc_file.h"
#include "metadata_cursor.h"

namespace abcd::abc {
namespace {
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
std::uint32_t Take(std::variant<std::uint32_t, MetadataError> value) {
  if (const auto* error = std::get_if<MetadataError>(&value)) throw *error;
  return std::get<std::uint32_t>(value);
}
void CheckReference(std::uint32_t offset, std::uint32_t size) {
  if (offset < kDynamicHeaderSize || offset >= size)
    throw MetadataError::kReference;
}
}  // namespace
DebugResult ReadDebugInfo(const corpus::AbcFile& file, const Header& header,
                          EntityOffset offset, DebugBudget budget) {
  const auto* layout = std::get_if<DynamicLayout>(&header.layout);
  if (!layout || header.kind != AbcKind::kDynamic || !Supported(header.profile))
    return MetadataError::kUnsupportedProfile;
  if (header.file_size != file.Size() || file.CheckUnchanged())
    return MetadataError::kFileChanged;
  try {
    // runtime_core 3e147bb, DebugInfoDataAccessor: parameter offsets are plain
    // ULEB entity IDs (zero absent), NOT the DWARF/Dex offset-plus-one
    // encoding.
    CheckReference(offset.value, header.file_size);
    MetadataCursor cursor(file, offset.value, header.file_size);
    DebugInfo info;
    info.line_start = Take(cursor.Uleb());
    const auto count = Take(cursor.Uleb());
    if (count > header.file_size - cursor.Position())
      return MetadataError::kTruncated;
    using Parameter = std::optional<EntityOffset>;
    if (count > budget.bytes / sizeof(Parameter))
      return MetadataError::kResourceLimit;
    info.parameters.reserve(count);
    if (info.parameters.capacity() > budget.bytes / sizeof(Parameter))
      return MetadataError::kResourceLimit;
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto name = Take(cursor.Uleb());
      if (name == 0)
        info.parameters.push_back(std::nullopt);
      else {
        CheckReference(name, header.file_size);
        info.parameters.push_back(EntityOffset{name});
      }
    }
    info.constant_pool_bytes = Take(cursor.Uleb());
    info.constant_pool = {cursor.Position()};
    if (!cursor.Skip(info.constant_pool_bytes))
      return MetadataError::kTruncated;
    const auto program_index = Take(cursor.Uleb());
    info.end = {cursor.Position()};
    const auto table = layout->line_number_programs;
    constexpr auto kIndexBytes = sizeof(std::uint32_t);
    if (program_index >= table.count || table.offset < kDynamicHeaderSize ||
        table.offset > header.file_size || table.offset % kIndexBytes != 0 ||
        table.count > (header.file_size - table.offset) / kIndexBytes)
      return MetadataError::kIndexRange;
    MetadataCursor index(file, table.offset + program_index * kIndexBytes,
                         header.file_size);
    info.line_program = {Take(index.U32())};
    CheckReference(info.line_program.value, header.file_size);
    if (file.CheckUnchanged()) return MetadataError::kFileChanged;
    return info;
  } catch (MetadataError error) {
    return error;
  } catch (const std::bad_alloc&) {
    return MetadataError::kResourceLimit;
  } catch (const std::length_error&) {
    return MetadataError::kResourceLimit;
  }
}
}  // namespace abcd::abc
