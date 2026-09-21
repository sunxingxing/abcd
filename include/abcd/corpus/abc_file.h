#ifndef ABCD_CORPUS_ABC_FILE_H_
#define ABCD_CORPUS_ABC_FILE_H_

#include <sys/stat.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <variant>

#include "abcd/abc/header.h"

namespace abcd::corpus {

enum class FileError {
  kOpen,
  kStat,
  kNotRegular,
  kRead,
  kBounds,
  kChanged,
  kHash
};

struct Sha256 {
  std::array<std::uint8_t, 32> bytes{};
  auto operator<=>(const Sha256&) const = default;
};

struct FileFingerprint {
  Sha256 sha256;
  std::uint64_t size = 0;
  std::uint32_t adler32 = 1;
  std::array<std::uint8_t, abc::kMaxHeaderSize> prefix{};
  std::size_t prefix_size = 0;
};

// Owns one read-only descriptor. Readers use positional IO, never a shared
// cursor. Invariant: hash and classification use this same open file, not a
// reopened path.
class AbcFile {
 public:
  static std::variant<std::unique_ptr<AbcFile>, FileError> Open(
      const std::filesystem::path& path);
  // Pin each directory while traversing an untrusted relative corpus path;
  // reject symlinks and parent components rather than checking then reopening.
  static std::variant<std::unique_ptr<AbcFile>, FileError> OpenBeneath(
      const std::filesystem::path& root, const std::filesystem::path& relative);
  ~AbcFile();
  AbcFile(const AbcFile&) = delete;
  AbcFile& operator=(const AbcFile&) = delete;

  std::uint64_t Size() const;
  std::optional<FileError> ReadAt(std::uint64_t offset,
                                  std::span<std::uint8_t> bytes) const;
  std::optional<FileError> CheckUnchanged() const;
  std::variant<FileFingerprint, FileError> Fingerprint() const;

 private:
  AbcFile(int fd, const struct stat& stamp);
  std::optional<FileError> ValidateRegular();
  int fd_;
  struct stat stamp_;
};

}  // namespace abcd::corpus
#endif  // ABCD_CORPUS_ABC_FILE_H_
