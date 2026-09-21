#ifndef ABCD_CORPUS_MANIFEST_H_
#define ABCD_CORPUS_MANIFEST_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "abcd/corpus/detect.h"

namespace abcd::corpus {

inline constexpr std::uint32_t kManifestSchema = 1;
// Bounded DOM input and entry counts leave room within the 1G process budget.
inline constexpr std::size_t kMaxManifestBytes = 8 * 1024 * 1024;
inline constexpr std::size_t kMaxManifestEntries = 10000;

struct CorpusEntry {
  std::string corpus_id;
  std::filesystem::path relative_path;
  std::uint64_t size = 0;
  Sha256 sha256;
  Detection detection;
  std::uint32_t detector_revision = kDetectorRevision;
  std::int64_t checked_at = 0;  // UTC Unix seconds, not evidence of validity.
  bool operator==(const CorpusEntry&) const = default;
};

enum class ManifestError {
  kOpen, kLockBusy, kRead, kTooLarge, kSchema, kWrite, kRename,
  kCommittedNotDurable
};

using ManifestResult = std::variant<std::vector<CorpusEntry>, ManifestError>;

// Lock spans load, scan, and save. Never unlink the lock inode: waiters must
// coordinate on the same file even after an atomic manifest replacement.
class ManifestStore {
 public:
  static std::variant<std::unique_ptr<ManifestStore>, ManifestError> Open(
      const std::filesystem::path& path);
  ~ManifestStore();
  ManifestStore(const ManifestStore&) = delete;
  ManifestStore& operator=(const ManifestStore&) = delete;

  ManifestResult Load() const;
  std::optional<ManifestError> Save(const std::vector<CorpusEntry>& entries) const;

 private:
  explicit ManifestStore(std::filesystem::path path);
  std::filesystem::path path_;
  int lock_fd_ = -1;
};

}  // namespace abcd::corpus
#endif  // ABCD_CORPUS_MANIFEST_H_
