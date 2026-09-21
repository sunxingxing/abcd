#include "abcd/corpus/manifest.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

#include "manifest_json.h"

namespace abcd::corpus {
namespace {
constexpr int kInvalidFd = -1;

class ScopedFd {
 public:
  explicit ScopedFd(int fd) : fd_(fd) {}
  ~ScopedFd() {
    if (fd_ >= 0) close(fd_);
  }
  ScopedFd(const ScopedFd&) = delete;
  ScopedFd& operator=(const ScopedFd&) = delete;
  int Get() const { return fd_; }

 private:
  int fd_;
};

class TempManifest {
 public:
  explicit TempManifest(const std::filesystem::path& target) {
    path_ = target.string() + ".tmp.XXXXXX";
    fd_ = mkstemp(path_.data());
  }
  ~TempManifest() {
    if (fd_ >= 0) close(fd_);
    if (fd_ >= 0 && !renamed_) unlink(path_.c_str());
  }
  TempManifest(const TempManifest&) = delete;
  TempManifest& operator=(const TempManifest&) = delete;
  int Get() const { return fd_; }
  bool Rename(const std::filesystem::path& target) {
    if (rename(path_.c_str(), target.c_str()) != 0) return false;
    renamed_ = true;
    return true;
  }

 private:
  std::string path_;
  int fd_ = kInvalidFd;
  bool renamed_ = false;
};
}  // namespace

ManifestStore::ManifestStore(std::filesystem::path path)
    : path_(std::move(path)) {}

ManifestStore::~ManifestStore() {
  if (lock_fd_ >= 0) close(lock_fd_);
}

std::variant<std::unique_ptr<ManifestStore>, ManifestError> ManifestStore::Open(
    const std::filesystem::path& path) {
  auto store = std::unique_ptr<ManifestStore>(new ManifestStore(path));
  const auto lock_path = path.string() + ".lock";
  constexpr mode_t kOwnerReadWrite = S_IRUSR | S_IWUSR;
  store->lock_fd_ = open(lock_path.c_str(),
                         O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK,
                         kOwnerReadWrite);
  if (store->lock_fd_ < 0) return ManifestError::kOpen;
  struct stat stamp {};
  if (fstat(store->lock_fd_, &stamp) != 0 || !S_ISREG(stamp.st_mode)) {
    return ManifestError::kOpen;
  }
  if (flock(store->lock_fd_, LOCK_EX | LOCK_NB) != 0) {
    return errno == EWOULDBLOCK ? ManifestError::kLockBusy
                                : ManifestError::kOpen;
  }
  return store;
}

ManifestResult ManifestStore::Load() const {
  ScopedFd file(
      open(path_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  if (file.Get() < 0) {
    if (errno == ENOENT) return std::vector<CorpusEntry>{};
    return ManifestError::kRead;
  }
  struct stat stamp {};
  if (fstat(file.Get(), &stamp) != 0 || !S_ISREG(stamp.st_mode) ||
      stamp.st_size < 0) {
    return ManifestError::kRead;
  }
  if (static_cast<std::uint64_t>(stamp.st_size) > kMaxManifestBytes) {
    return ManifestError::kTooLarge;
  }
  std::string bytes(static_cast<std::size_t>(stamp.st_size), '\0');
  std::size_t position = 0;
  while (position < bytes.size()) {
    const auto count =
        read(file.Get(), bytes.data() + position, bytes.size() - position);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return ManifestError::kRead;
    position += static_cast<std::size_t>(count);
  }
  return DecodeManifest(bytes);
}

std::optional<ManifestError> ManifestStore::Save(
    const std::vector<CorpusEntry>& entries) const {
  auto encoded = EncodeManifest(entries);
  if (const auto* error = std::get_if<ManifestError>(&encoded)) return *error;
  const auto& bytes = std::get<std::string>(encoded);
  const auto parent = path_.has_parent_path() ? path_.parent_path()
                                              : std::filesystem::path(".");
  ScopedFd directory(open(parent.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY));
  if (directory.Get() < 0) return ManifestError::kWrite;
  TempManifest temporary(path_);
  if (temporary.Get() < 0) return ManifestError::kWrite;
  std::size_t position = 0;
  while (position < bytes.size()) {
    const auto count = write(temporary.Get(), bytes.data() + position,
                             bytes.size() - position);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return ManifestError::kWrite;
    position += static_cast<std::size_t>(count);
  }
  if (fsync(temporary.Get()) != 0) return ManifestError::kWrite;
  if (!temporary.Rename(path_)) return ManifestError::kRename;
  // Rename committed a complete file. Sync failure cannot promise rollback.
  if (fsync(directory.Get()) != 0) return ManifestError::kCommittedNotDurable;
  return std::nullopt;
}
}  // namespace abcd::corpus
