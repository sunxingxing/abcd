#ifndef ABCD_ABC_RECORD_ATTRIBUTES_H_
#define ABCD_ABC_RECORD_ATTRIBUTES_H_

#include "abcd/abc/string_entity.h"

namespace abcd::abc {

struct RecordAttributes {
  // Presence differs from false: the merged runtime uses field presence when
  // deciding which records to publish. Do not collapse these to default bools.
  std::optional<bool> common_js;
  std::optional<bool> shared;
  std::optional<bool> top_level_await;
  std::optional<EntityOffset> json_content;
  std::optional<EntityOffset> module;
  std::optional<EntityOffset> request_phases;
};
enum class AttributeError {
  kUnsupportedProfile,
  kFileChanged,
  kFieldRange,
  kOwnerMismatch,
  kName,
  kMissingValue,
  kTypeReference,
  kWideValue,
  kReference,
};
struct AttributeFailure {
  AttributeError kind;
  EntityOffset field;
  std::optional<MetadataError> metadata_error;
};
using AttributeResult = std::variant<RecordAttributes, AttributeFailure>;

enum class FilePackaging { kMerged, kBundle };
using PackagingResult = std::variant<FilePackaging, AttributeFailure>;

// Requires a local-field index from this immutable file. Mirrors the pinned
// CheckIsBundlePack name-presence rule, independent of values and path names.
[[nodiscard]] PackagingResult DetectFilePackaging(
    const corpus::AbcFile& file, const Header& header,
    const MetadataIndex& index, StringBudget name_budget = {});

// Merged-record field convention only. Does not discover file packaging or
// implement legacy unmerged special classes. Names are decoded one at a time;
// runtime-recognized duplicates overwrite in file order, as in the pinned VM.
[[nodiscard]] AttributeResult ReadMergedRecordAttributes(
    const corpus::AbcFile& file, const Header& header,
    const MetadataIndex& index, const RecordInfo& record,
    StringBudget name_budget = {});

}  // namespace abcd::abc
#endif  // ABCD_ABC_RECORD_ATTRIBUTES_H_
