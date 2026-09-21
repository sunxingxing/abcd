#include "abcd/corpus/scan.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#include <zlib.h>

namespace {
using namespace abcd::corpus;

void Check(bool condition) {
  if (!condition) {
    std::cerr << "scan_test failed\n";
    std::abort();
  }
}

class Fixture {
 public:
  Fixture() {
    char pattern[] = "scan_test.XXXXXX";
    const auto* path = mkdtemp(pattern);
    Check(path != nullptr);
    directory = path;
    roots = {{"batch", directory / "batch"}, {"paired", directory / "paired"}};
    for (const auto& root : roots) std::filesystem::create_directory(root.path);
    manifest = directory / "index.json";
  }
  ~Fixture() { std::filesystem::remove_all(directory); }
  std::filesystem::path directory;
  std::filesystem::path manifest;
  std::vector<CorpusRoot> roots;
};

void WriteWord(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
  for (unsigned int i = 0; i < 4; ++i) bytes[offset+i] = value >> (8*i);
}

void WriteAbc(const std::filesystem::path& path, abcd::abc::AbcVersion version) {
  constexpr abcd::abc::AbcVersion kStatic{{0, 1, 0, 7}};
  std::vector<std::uint8_t> bytes(version == kStatic ? 72 : 60);
  const std::string magic = "PANDA";
  std::copy(magic.begin(), magic.end(), bytes.begin());
  std::copy(version.bytes.begin(), version.bytes.end(), bytes.begin() + 12);
  WriteWord(bytes, 16, bytes.size());
  WriteWord(bytes, 8, adler32(1, bytes.data()+12, bytes.size()-12));
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  output.close();
  Check(!output.fail());
}

ScanStats Scan(const Fixture& fixture) {
  auto result = ScanCorpus(fixture.manifest, fixture.roots);
  Check(std::holds_alternative<ScanStats>(result));
  return std::get<ScanStats>(result);
}

void ReusesContentAndInvalidatesChangedBytesAndRules() {
  Fixture fixture;
  const auto first = fixture.roots[0].path / "first.abc";
  const auto duplicate = fixture.roots[1].path / "duplicate.abc";
  WriteAbc(first, {{12, 0, 6, 0}});
  std::filesystem::copy_file(first, duplicate);
  auto stats = Scan(fixture);
  Check(stats.files == 2 && stats.classified == 1 && stats.reused == 1);
  Check(stats.dynamic == 2 && stats.unique_contents == 1);
  stats = Scan(fixture);
  Check(stats.classified == 0 && stats.reused == 2);
  Check(stats.bytes_hashed == 120);
  WriteAbc(first, {{9, 0, 0, 0}});
  stats = Scan(fixture);
  Check(stats.classified == 1 && stats.reused == 1 && stats.unique_contents == 2);
  {
    auto opened = ManifestStore::Open(fixture.manifest);
    auto store = std::move(std::get<std::unique_ptr<ManifestStore>>(opened));
    auto entries = std::get<std::vector<CorpusEntry>>(store->Load());
    for (auto& entry : entries) ++entry.detector_revision;
    Check(!store->Save(entries));
  }
  stats = Scan(fixture);
  Check(stats.classified == 2 && stats.reused == 0);
}

void PreservesOtherRootsAndSeparatesKinds() {
  Fixture fixture;
  WriteAbc(fixture.roots[0].path / "static.abc", {{0, 1, 0, 7}});
  WriteAbc(fixture.roots[0].path / "unknown.abc", {{99, 0, 0, 0}});
  { std::ofstream output(fixture.roots[1].path / "bad.abc"); output << "bad"; }
  auto stats = Scan(fixture);
  Check(stats.static_abc == 1 && stats.unknown == 1 && stats.invalid == 1);
  stats = Scan(fixture);
  Check(stats.classified == 0 && stats.reused == 3 && stats.static_abc == 1);
  const std::array one_root{fixture.roots.front()};
  auto result = ScanCorpus(fixture.manifest, one_root);
  Check(std::get<ScanStats>(result).files == 2);
  auto opened = ManifestStore::Open(fixture.manifest);
  auto store = std::move(std::get<std::unique_ptr<ManifestStore>>(opened));
  Check(std::get<std::vector<CorpusEntry>>(store->Load()).size() == 3);
}

void RejectsBadRootsAndDoesNotFollowSymlinks() {
  Fixture fixture;
  std::filesystem::create_symlink("missing.abc", fixture.roots[0].path / "link.abc");
  auto stats = Scan(fixture);
  Check(stats.files == 0 && stats.symlinks_skipped == 1);
  fixture.roots[0].path /= "missing";
  const auto result = ScanCorpus(fixture.manifest, fixture.roots);
  Check(std::get<ScanFailure>(result).issue == ScanIssue::kInvalidRoot);
}

void RejectsSymlinkRoots() {
  Fixture fixture;
  const auto alias = fixture.directory / "alias";
  std::filesystem::create_directory_symlink(
      std::filesystem::absolute(fixture.roots[0].path), alias);
  const std::array spellings{alias, std::filesystem::path(alias.string() + "/"),
                             alias / "."};
  for (const auto& spelling : spellings) {
    const std::array roots{CorpusRoot{"alias", spelling}};
    const auto result = ScanCorpus(fixture.manifest, roots);
    Check(std::holds_alternative<ScanFailure>(result));
    Check(std::get<ScanFailure>(result).issue == ScanIssue::kInvalidRoot);
  }
}
}  // namespace

int main() {
  ReusesContentAndInvalidatesChangedBytesAndRules();
  PreservesOtherRootsAndSeparatesKinds();
  RejectsBadRootsAndDoesNotFollowSymlinks();
  RejectsSymlinkRoots();
}
