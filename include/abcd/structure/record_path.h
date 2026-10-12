#ifndef ABCD_STRUCTURE_RECORD_PATH_H_
#define ABCD_STRUCTURE_RECORD_PATH_H_

#include <span>
#include <string>
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
  kDescriptor,
  kComponentLimit,
  kPathLimit,
  kCollision,
  kResourceLimit,
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
// Prefers unique ABC-embedded package filename identities under the pinned
// build-tool format. Multiple entry/version namespaces stay distinct; Unicode
// scalar filenames use reversible UTF-8 percent encoding. Unsupported
// identities retain descriptor paths; conflicting names never become path
// authority. No sidecar input is read.
[[nodiscard]] std::variant<std::vector<RecordPath>, PathError>
PlanRecordSourcePaths(const corpus::AbcFile& file, const abc::Header& header,
                      const abc::MetadataIndex& index,
                      std::size_t budget_bytes = 16 * 1024 * 1024);
// Per-record form of the above (S3). The outer error is file-wide (budget
// only); each record carries its own path or typed error: unreadable identity
// evidence (kSourceEvidence), encoding limits, or membership in a case-folded
// collision group (kCollision, every member). A record whose evidence failed
// is excluded from namespace qualification and never names a file. Whether a
// read failure is process-level is the caller's decision
// (corpus::AbcFile::ReadFailed).
using RecordSourcePath = std::variant<RecordPath, PathError>;
[[nodiscard]] std::variant<std::vector<RecordSourcePath>, PathError>
PlanRecordSourcePathSet(const corpus::AbcFile& file, const abc::Header& header,
                        const abc::MetadataIndex& index,
                        std::size_t budget_bytes = 16 * 1024 * 1024);
}  // namespace abcd::structure
#endif
