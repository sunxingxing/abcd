#ifndef ABCD_STRUCTURE_RECORD_PATH_H_
#define ABCD_STRUCTURE_RECORD_PATH_H_

#include <string>
#include <span>
#include <variant>
#include <vector>

#include "abcd/abc/metadata_index.h"

namespace abcd::structure {
enum class SourcePathOrigin { kRecordDescriptor, kPackageIdentity };
struct RecordPath {
  std::string stem;
  SourcePathOrigin origin = SourcePathOrigin::kRecordDescriptor;
};
enum class PathError {
  kDescriptor, kComponentLimit, kPathLimit, kCollision, kResourceLimit,
  kSourceEvidence
};
// Deterministic relative ASCII path. Descriptor bytes are textual data, not
// host path authority. Entity suffix separates equal descriptors within a file.
[[nodiscard]] std::variant<RecordPath, PathError> PlanRecordPath(
    const abc::RecordInfo& record);
// Source stems follow descriptor components, not debug-sidecar paths. No
// guessed source root/extension. Reject case-folded collisions before writing;
// archive stems remain separately entity-qualified for diagnostic artifacts.
[[nodiscard]] std::variant<std::vector<RecordPath>, PathError>
PlanRecordSourcePaths(std::span<const abc::RecordInfo> records,
                      std::size_t budget_bytes = 16 * 1024 * 1024);
// Prefers unique ABC-embedded same-entry-package filename identities under
// the pinned build-tool format. Unsupported identities retain descriptor paths;
// conflicting names never become path authority. No sidecar input is read.
[[nodiscard]] std::variant<std::vector<RecordPath>, PathError>
PlanRecordSourcePaths(const corpus::AbcFile& file, const abc::Header& header,
                      const abc::MetadataIndex& index,
                      std::size_t budget_bytes = 16 * 1024 * 1024);
}  // namespace abcd::structure
#endif
