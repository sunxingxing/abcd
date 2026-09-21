#include "abcd/corpus/abc_file.h"

#include <fcntl.h>
#include <openssl/evp.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <memory>

namespace abcd::corpus {
namespace {
// Fixed streaming capacity, independent of file size and worker count.
constexpr std::size_t kStreamBytes = 64 * 1024;
// Source: pinned runtime_core libpandafile/file.cpp, ValidateChecksum.
constexpr std::uint64_t kChecksumStart = 12;
constexpr std::uint32_t kAdlerSeed = 1;

bool SameTime(const timespec& left, const timespec& right) {
  return left.tv_sec == right.tv_sec && left.tv_nsec == right.tv_nsec;
}
}  // namespace

AbcFile::AbcFile(int fd, const struct stat& stamp) : fd_(fd), stamp_(stamp) {}

AbcFile::~AbcFile() {
  if (fd_ >= 0) close(fd_);
}

std::variant<std::unique_ptr<AbcFile>, FileError> AbcFile::Open(
    const std::filesystem::path& path) {
  // Allocate ownership before opening: allocation failure must not leak a fd.
  auto file = std::unique_ptr<AbcFile>(new AbcFile(-1, {}));
  // Reject special files after fstat without hanging on a FIFO before that
  // check.
  file->fd_ =
      open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (file->fd_ < 0) return FileError::kOpen;
  if (fstat(file->fd_, &file->stamp_) != 0) return FileError::kStat;
  if (!S_ISREG(file->stamp_.st_mode) || file->stamp_.st_size < 0) {
    return FileError::kNotRegular;
  }
  return file;
}

std::uint64_t AbcFile::Size() const {
  return static_cast<std::uint64_t>(stamp_.st_size);
}

std::optional<FileError> AbcFile::ReadAt(std::uint64_t offset,
                                         std::span<std::uint8_t> bytes) const {
  if (offset > Size() || bytes.size() > Size() - offset) {
    return FileError::kBounds;
  }
  while (!bytes.empty()) {
    const auto count =
        pread(fd_, bytes.data(), std::min(bytes.size(), kStreamBytes),
              static_cast<off_t>(offset));
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return FileError::kRead;
    const auto consumed = static_cast<std::size_t>(count);
    bytes = bytes.subspan(consumed);
    offset += consumed;
  }
  return std::nullopt;
}

std::optional<FileError> AbcFile::CheckUnchanged() const {
  struct stat current {};
  if (fstat(fd_, &current) != 0) return FileError::kStat;
  if (current.st_size != stamp_.st_size || current.st_ino != stamp_.st_ino ||
      current.st_dev != stamp_.st_dev ||
      !SameTime(current.st_mtim, stamp_.st_mtim) ||
      !SameTime(current.st_ctim, stamp_.st_ctim)) {
    return FileError::kChanged;
  }
  return std::nullopt;
}

std::variant<FileFingerprint, FileError> AbcFile::Fingerprint() const {
  if (const auto error = CheckUnchanged()) return *error;
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest(
      EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  if (!digest || EVP_DigestInit_ex(digest.get(), EVP_sha256(), nullptr) != 1) {
    return FileError::kHash;
  }
  FileFingerprint result;
  result.size = Size();
  result.adler32 = kAdlerSeed;
  std::array<std::uint8_t, kStreamBytes> buffer;
  for (std::uint64_t offset = 0; offset < result.size;) {
    const auto count = static_cast<std::size_t>(
        std::min<std::uint64_t>(buffer.size(), result.size - offset));
    auto chunk = std::span(buffer).first(count);
    if (const auto error = ReadAt(offset, chunk)) return *error;
    if (EVP_DigestUpdate(digest.get(), chunk.data(), chunk.size()) != 1) {
      return FileError::kHash;
    }
    if (offset == 0) {
      result.prefix_size = std::min(result.prefix.size(), chunk.size());
      std::copy_n(chunk.begin(), result.prefix_size, result.prefix.begin());
    }
    const auto skip = static_cast<std::size_t>(
        offset < kChecksumStart
            ? std::min<std::uint64_t>(kChecksumStart - offset, chunk.size())
            : 0);
    const auto payload = chunk.subspan(skip);
    result.adler32 = static_cast<std::uint32_t>(adler32(
        result.adler32, payload.data(), static_cast<uInt>(payload.size())));
    offset += count;
  }
  unsigned int digest_size = 0;
  if (EVP_DigestFinal_ex(digest.get(), result.sha256.bytes.data(),
                         &digest_size) != 1 ||
      digest_size != result.sha256.bytes.size()) {
    return FileError::kHash;
  }
  if (const auto error = CheckUnchanged()) return *error;
  return result;
}

}  // namespace abcd::corpus
