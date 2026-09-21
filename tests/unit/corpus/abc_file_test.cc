#include "abcd/corpus/abc_file.h"

#include <unistd.h>
#include <sys/wait.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace {
using abcd::corpus::AbcFile;
using abcd::corpus::FileError;
using abcd::corpus::FileFingerprint;

void Check(bool condition) {
  if (!condition) {
    std::cerr << "abc_file_test failed\n";
    std::abort();
  }
}

class FixtureFile {
 public:
  FixtureFile() {
    char pattern[] = "abc_file_test.XXXXXX";
    const int fd = mkstemp(pattern);
    Check(fd >= 0);
    Check(close(fd) == 0);
    path_ = pattern;
  }
  ~FixtureFile() { std::filesystem::remove(path_); }
  const std::filesystem::path& path() const { return path_; }
  void Write(const std::string& bytes) {
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    Check(!stream.fail());
  }
 private:
  std::filesystem::path path_;
};

std::unique_ptr<AbcFile> Open(const FixtureFile& fixture) {
  auto opened = AbcFile::Open(fixture.path());
  Check(std::holds_alternative<std::unique_ptr<AbcFile>>(opened));
  return std::move(std::get<std::unique_ptr<AbcFile>>(opened));
}

void ReadsWithCheckedOffsets() {
  FixtureFile fixture;
  fixture.Write("abc");
  auto file = Open(fixture);
  std::array<std::uint8_t, 2> bytes{};
  Check(!file->ReadAt(1, bytes));
  Check(bytes[0] == 'b' && bytes[1] == 'c');
  Check(file->ReadAt(2, bytes) == FileError::kBounds);
  Check(file->ReadAt(std::numeric_limits<std::uint64_t>::max(), bytes) ==
        FileError::kBounds);
  Check(!file->ReadAt(3, {}));
  Check(file->ReadAt(4, {}) == FileError::kBounds);
  Check(!file->CheckUnchanged());
}

void HashesKnownEmptyDigest() {
  FixtureFile fixture;
  auto file = Open(fixture);
  auto result = file->Fingerprint();
  Check(std::holds_alternative<FileFingerprint>(result));
  const auto& fingerprint = std::get<FileFingerprint>(result);
  constexpr std::array<std::uint8_t, 32> kEmptySha256 = {
      0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
      0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};
  Check(fingerprint.sha256.bytes == kEmptySha256);
  Check(fingerprint.size == 0 && fingerprint.prefix_size == 0);
  Check(fingerprint.adler32 == 1);
}

void HashesMultipleChunksAndPreservesPrefix() {
  FixtureFile fixture;
  // Independent known vector: SHA-256 of 100000 ASCII 'a' bytes.
  fixture.Write(std::string(100000, 'a'));
  auto file = Open(fixture);
  auto result = file->Fingerprint();
  Check(std::holds_alternative<FileFingerprint>(result));
  const auto& fingerprint = std::get<FileFingerprint>(result);
  constexpr std::array<std::uint8_t, 32> kSha256 = {
      0x6d,0x1c,0xf2,0x2d,0x7c,0xc0,0x9b,0x08,0x5d,0xfc,0x25,0xee,0x1a,0x1f,0x3a,0xe0,
      0x26,0x58,0x04,0xc6,0x07,0xbc,0x20,0x74,0xad,0x25,0x3b,0xcc,0x82,0xfd,0x81,0xee};
  Check(fingerprint.sha256.bytes == kSha256);
  Check(fingerprint.size == 100000);
  Check(fingerprint.prefix_size == abcd::abc::kMaxHeaderSize);
  for (auto byte : fingerprint.prefix) Check(byte == 'a');
  // Adler32 excludes magic/checksum, exactly the first 12 bytes.
  std::uint32_t a = 1;
  std::uint32_t b = 0;
  constexpr std::uint32_t kAdlerModulus = 65521;
  for (std::size_t i = 12; i < fingerprint.size; ++i) {
    a = (a + 'a') % kAdlerModulus;
    b = (b + a) % kAdlerModulus;
  }
  Check(fingerprint.adler32 == (b << 16 | a));
}

void DetectsMutationAndRejectsNonfiles() {
  FixtureFile fixture;
  auto file = Open(fixture);
  fixture.Write("changed");
  Check(file->CheckUnchanged() == FileError::kChanged);
  auto result = file->Fingerprint();
  Check(std::get<FileError>(result) == FileError::kChanged);
  auto directory = AbcFile::Open(".");
  Check(std::get<FileError>(directory) == FileError::kNotRegular);
  auto missing = AbcFile::Open(fixture.path() / "missing");
  Check(std::get<FileError>(missing) == FileError::kOpen);
}

void RejectsFifoWithoutBlocking() {
  FixtureFile fixture;
  Check(std::filesystem::remove(fixture.path()));
  constexpr mode_t kOwnerReadWrite = S_IRUSR | S_IWUSR;
  Check(mkfifo(fixture.path().c_str(), kOwnerReadWrite) == 0);
  const pid_t child = fork();
  Check(child >= 0);
  if (child == 0) {
    constexpr unsigned int kHangGuardSeconds = 2;
    alarm(kHangGuardSeconds);
    const auto result = AbcFile::Open(fixture.path());
    const auto* error = std::get_if<FileError>(&result);
    _exit(error && *error == FileError::kNotRegular ? EXIT_SUCCESS : EXIT_FAILURE);
  }
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  Check(std::filesystem::remove(fixture.path()));
  Check(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS);
}
}  // namespace

int main() {
  ReadsWithCheckedOffsets();
  HashesKnownEmptyDigest();
  HashesMultipleChunksAndPreservesPrefix();
  DetectsMutationAndRejectsNonfiles();
  RejectsFifoWithoutBlocking();
}
