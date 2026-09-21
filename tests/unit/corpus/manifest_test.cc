#include "abcd/corpus/manifest.h"

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using namespace abcd::corpus;

void Check(bool condition) {
  if (!condition) {
    std::cerr << "manifest_test failed\n";
    std::abort();
  }
}

class FixtureDirectory {
 public:
  FixtureDirectory() {
    char pattern[] = "manifest_test.XXXXXX";
    const char* path = mkdtemp(pattern);
    Check(path != nullptr);
    path_ = path;
  }
  ~FixtureDirectory() { std::filesystem::remove_all(path_); }
  std::filesystem::path manifest() const { return path_ / "index.json"; }
 private:
  std::filesystem::path path_;
};

std::unique_ptr<ManifestStore> Open(const FixtureDirectory& fixture) {
  auto result = ManifestStore::Open(fixture.manifest());
  Check(std::holds_alternative<std::unique_ptr<ManifestStore>>(result));
  return std::move(std::get<std::unique_ptr<ManifestStore>>(result));
}

CorpusEntry StaticEntry() {
  CorpusEntry entry;
  entry.corpus_id = "batch";
  entry.relative_path = "nested/module.abc";
  entry.size = 100;
  entry.sha256.bytes.fill(42);
  entry.detection.kind = SampleKind::kStatic;
  entry.detection.version = abcd::abc::AbcVersion{{0, 0, 0, 6}};
  entry.detection.evidence = {EvidenceRule::kEtsClass, 80, 5};
  entry.checked_at = 1700000000;
  return entry;
}

void RoundTripAndLocks() {
  FixtureDirectory fixture;
  auto store = Open(fixture);
  Check(std::get<std::vector<CorpusEntry>>(store->Load()).empty());
  auto competing = ManifestStore::Open(fixture.manifest());
  Check(std::get<ManifestError>(competing) == ManifestError::kLockBusy);
  const std::vector<CorpusEntry> entries{StaticEntry()};
  Check(!store->Save(entries));
  Check(std::get<std::vector<CorpusEntry>>(store->Load()) == entries);
  store.reset();
  store = Open(fixture);
  Check(std::get<std::vector<CorpusEntry>>(store->Load()) == entries);
}

void RejectsCorruptAndOversizedInput() {
  FixtureDirectory fixture;
  auto store = Open(fixture);
  {
    std::ofstream output(fixture.manifest());
    output << "{\"schema_version\":1,\"entries\":[{\"kind\":\"bogus\"}]}";
  }
  Check(std::get<ManifestError>(store->Load()) == ManifestError::kSchema);
  {
    std::ofstream output(fixture.manifest());
    output.seekp(kMaxManifestBytes);
    output.put(' ');
  }
  Check(std::get<ManifestError>(store->Load()) == ManifestError::kTooLarge);
}

void RejectsInvalidPathsAndDuplicateEntries() {
  FixtureDirectory fixture;
  auto store = Open(fixture);
  const auto entry = StaticEntry();
  Check(!store->Save({entry}));
  auto invalid = entry;
  invalid.relative_path = "../outside.abc";
  Check(store->Save({invalid}) == ManifestError::kSchema);
  Check(store->Save({entry, entry}) == ManifestError::kSchema);
  Check(std::get<std::vector<CorpusEntry>>(store->Load()) ==
        std::vector<CorpusEntry>{entry});
}

void FailedWritePreservesCommittedManifest() {
  FixtureDirectory fixture;
  auto store = Open(fixture);
  const std::vector<CorpusEntry> entries{StaticEntry()};
  Check(!store->Save(entries));
  const pid_t child = fork();
  Check(child >= 0);
  if (child == 0) {
    std::signal(SIGXFSZ, SIG_IGN);
    const rlimit limit{1, 1};
    if (setrlimit(RLIMIT_FSIZE, &limit) != 0) _exit(EXIT_FAILURE);
    _exit(store->Save({}) == ManifestError::kWrite ? EXIT_SUCCESS : EXIT_FAILURE);
  }
  int status = 0;
  Check(waitpid(child, &status, 0) == child);
  Check(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS);
  Check(std::get<std::vector<CorpusEntry>>(store->Load()) == entries);
}
}  // namespace

int main() {
  RoundTripAndLocks();
  RejectsCorruptAndOversizedInput();
  RejectsInvalidPathsAndDuplicateEntries();
  FailedWritePreservesCommittedManifest();
}
