#include "abcd/abc/pool_reference.h"

#include <algorithm>
#include <array>
#include <iterator>

#include "abcd/abc/byte_reader.h"
#include "abcd/corpus/abc_file.h"

namespace abcd::abc {
namespace {
bool UsesRegionalPool(ProfileId profile) {
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

std::variant<PoolReference, MetadataError> ResolvePoolReference(
    const corpus::AbcFile& file, const Header& header,
    const MetadataIndex& index, EntityOffset enclosing_method, PoolIndex slot,
    PoolEntityKind kind) {
  if (header.kind != AbcKind::kDynamic ||
      !std::holds_alternative<DynamicLayout>(header.layout) ||
      !UsesRegionalPool(header.profile))
    return MetadataError::kUnsupportedProfile;
  if (file.Size() != header.file_size || file.CheckUnchanged())
    return MetadataError::kFileChanged;
  switch (kind) {
    case PoolEntityKind::kMethod:
    case PoolEntityKind::kString:
    case PoolEntityKind::kLiteralArray:
      break;
    default:
      return MetadataError::kReference;
  }
  const auto it = std::upper_bound(
      index.regions.begin(), index.regions.end(), enclosing_method.value,
      [](std::uint32_t value, const IndexRegion& region) {
        return value < region.start;
      });
  if (it == index.regions.begin()) return MetadataError::kRegionRange;
  const auto& region = *std::prev(it);
  if (enclosing_method.value >= region.end) return MetadataError::kRegionRange;
  // Pinned ConstantPool::GetEntityId uses method_idx for all three roles.
  // Literal payload EntityIds must NOT be passed through this indirection.
  constexpr std::uint32_t kEntityOffsetBytes = sizeof(std::uint32_t);
  const auto table = region.methods;
  if (table.offset > header.file_size ||
      table.count > (header.file_size - table.offset) / kEntityOffsetBytes ||
      slot.value >= table.count)
    return MetadataError::kIndexRange;
  std::array<std::uint8_t, kEntityOffsetBytes> bytes{};
  if (file.ReadAt(table.offset + slot.value * kEntityOffsetBytes, bytes))
    return MetadataError::kRead;
  ByteReader reader(bytes);
  const auto offset = std::get<std::uint32_t>(reader.ReadU32Le());
  if (offset < kDynamicHeaderSize || offset >= header.file_size)
    return MetadataError::kReference;
  if (file.CheckUnchanged()) return MetadataError::kFileChanged;
  return PoolReference{kind, {offset}};
}

}  // namespace abcd::abc
