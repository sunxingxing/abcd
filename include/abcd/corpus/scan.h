#ifndef ABCD_CORPUS_SCAN_H_
#define ABCD_CORPUS_SCAN_H_

#include <span>

#include "abcd/corpus/manifest.h"

namespace abcd::corpus {

struct CorpusRoot {
  std::string id;
  std::filesystem::path path;
};

struct ScanStats {
  std::uint64_t files = 0;
  std::uint64_t unique_contents = 0;
  std::uint64_t bytes_hashed = 0;
  std::uint64_t classified = 0;
  std::uint64_t reused = 0;
  std::uint64_t dynamic = 0;
  std::uint64_t static_abc = 0;
  std::uint64_t unknown = 0;
  std::uint64_t invalid = 0;
  std::uint64_t symlinks_skipped = 0;
};

enum class ScanIssue {
  kManifest,
  kFilesystem,
  kFile,
  kInvalidRoot,
  kLimit,
  kClock
};
struct ScanFailure {
  ScanIssue issue;
  std::filesystem::path path;
  std::optional<FileError> file_error;
  std::optional<ManifestError> manifest_error;
};

using ScanResult = std::variant<ScanStats, ScanFailure>;
// Selected corpus roots are replaced as snapshots; other corpus entries
// survive. Content hashes are always recomputed, while current-revision
// detections reuse immutable values by hash. Classification is not
// decompilation support.
ScanResult ScanCorpus(const std::filesystem::path& manifest,
                      std::span<const CorpusRoot> roots);

}  // namespace abcd::corpus
#endif  // ABCD_CORPUS_SCAN_H_
