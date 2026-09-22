#ifndef ABCD_STRUCTURE_RECORD_PATH_H_
#define ABCD_STRUCTURE_RECORD_PATH_H_

#include <string>
#include <variant>

#include "abcd/abc/metadata_index.h"

namespace abcd::structure {
struct RecordPath {
  std::string stem;
};
enum class PathError { kDescriptor, kComponentLimit, kPathLimit };
// Deterministic relative ASCII path. Descriptor bytes are textual data, not
// host path authority. Entity suffix separates equal descriptors within a file.
[[nodiscard]] std::variant<RecordPath, PathError> PlanRecordPath(
    const abc::RecordInfo& record);
}  // namespace abcd::structure
#endif
