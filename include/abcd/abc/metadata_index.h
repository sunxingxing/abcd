#ifndef ABCD_ABC_METADATA_INDEX_H_
#define ABCD_ABC_METADATA_INDEX_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "abcd/abc/header.h"

namespace abcd::corpus {
class AbcFile;
}

namespace abcd::abc {

struct EntityOffset {
  std::uint32_t value = 0;
  auto operator<=>(const EntityOffset&) const = default;
};

struct CodeInfo {
  EntityOffset offset;
  EntityOffset instruction_offset;
  std::uint32_t registers = 0;
  std::uint32_t arguments = 0;
  std::uint32_t instruction_bytes = 0;
  std::uint32_t try_count = 0;
};

struct RecordInfo {
  EntityOffset offset;
  std::string descriptor;
  std::uint32_t utf16_length = 0;
  std::uint32_t access_flags = 0;
  std::uint32_t field_count = 0;
  std::uint32_t method_count = 0;
  std::size_t first_method = 0;
};

struct MethodInfo {
  EntityOffset offset;
  EntityOffset owner_offset;
  std::optional<EntityOffset> proto_offset;
  std::string name;
  std::uint32_t name_utf16_length = 0;
  std::uint32_t access_flags = 0;
  std::optional<CodeInfo> code;
};

struct MetadataIndex {
  std::vector<RecordInfo> records;
  std::vector<MethodInfo> methods;
};

enum class MetadataError {
  kUnsupportedProfile,
  kFileChanged,
  kRead,
  kTruncated,
  kMalformedLeb,
  kBadString,
  kUnknownTag,
  kRegionRange,
  kRegionOverlap,
  kIndexRange,
  kReference,
  kOwnerMismatch,
  kCodeRange,
  kResourceLimit,
};

struct MetadataBudget {
  // This is a per-index allowance; callers must reserve it within the process
  // memory admission budget before starting concurrent indexes.
  std::size_t bytes = 64 * 1024 * 1024;
};

using MetadataResult = std::variant<MetadataIndex, MetadataError>;

[[nodiscard]] MetadataResult BuildMetadataIndex(
    const corpus::AbcFile& file, const Header& header,
    MetadataBudget budget = {});

}  // namespace abcd::abc

#endif  // ABCD_ABC_METADATA_INDEX_H_
