#include "abcd/corpus/scan.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <tuple>

namespace abcd::corpus {
namespace {
struct InputFile {
  std::string corpus_id;
  std::filesystem::path relative;
  std::filesystem::path absolute;
};

struct CachedDetection {
  std::uint64_t size;
  Detection detection;
};

std::optional<ScanFailure> Collect(
    std::span<const CorpusRoot> roots, std::vector<InputFile>* inputs,
    std::set<std::string>* selected, ScanStats* stats) {
  constexpr std::size_t kMaxCorpusIdBytes = 64;
  for (const auto& root : roots) {
    std::error_code error;
    auto checked_root = std::filesystem::absolute(root.path, error);
    if (error) return ScanFailure{ScanIssue::kInvalidRoot, root.path, {}, {}};
    // A trailing slash or dot makes symlink_status follow a directory link.
    // Remove only those suffixes; lexical '..' removal could change the target.
    while (checked_root != checked_root.root_path() &&
           (!checked_root.has_filename() || checked_root.filename() == ".")) {
      checked_root = checked_root.parent_path();
    }
    if (root.id.empty() || root.id.size() > kMaxCorpusIdBytes ||
        root.id.find('\0') != std::string::npos ||
        !selected->insert(root.id).second ||
        std::filesystem::is_symlink(checked_root, error) || error ||
        !std::filesystem::is_directory(checked_root, error) || error) {
      return ScanFailure{ScanIssue::kInvalidRoot, root.path, {}, {}};
    }
    const auto absolute = std::filesystem::canonical(checked_root, error);
    if (error) return ScanFailure{ScanIssue::kFilesystem, root.path, {}, {}};
    std::filesystem::recursive_directory_iterator it(absolute, error), end;
    if (error) return ScanFailure{ScanIssue::kFilesystem, absolute, {}, {}};
    for (; it != end; it.increment(error)) {
      if (error) return ScanFailure{ScanIssue::kFilesystem, absolute, {}, {}};
      const auto path = it->path();
      const bool symlink = it->is_symlink(error);
      if (error) return ScanFailure{ScanIssue::kFilesystem, path, {}, {}};
      if (symlink) {
        ++stats->symlinks_skipped;
        continue;
      }
      const bool regular = it->is_regular_file(error);
      if (error) return ScanFailure{ScanIssue::kFilesystem, path, {}, {}};
      if (regular && path.extension() == ".abc") {
        if (inputs->size() >= kMaxManifestEntries) {
          return ScanFailure{ScanIssue::kLimit, path, {}, {}};
        }
        inputs->push_back({root.id, path.lexically_relative(absolute), path});
      }
    }
    if (error) return ScanFailure{ScanIssue::kFilesystem, absolute, {}, {}};
  }
  std::sort(inputs->begin(), inputs->end(), [](const auto& left, const auto& right) {
    return std::tie(left.corpus_id, left.relative) <
           std::tie(right.corpus_id, right.relative);
  });
  return std::nullopt;
}

void CountKind(SampleKind kind, ScanStats* stats) {
  switch (kind) {
    case SampleKind::kDynamic: ++stats->dynamic; break;
    case SampleKind::kStatic: ++stats->static_abc; break;
    case SampleKind::kUnknown: ++stats->unknown; break;
    case SampleKind::kInvalid: ++stats->invalid; break;
  }
}
}  // namespace

ScanResult ScanCorpus(const std::filesystem::path& manifest,
                      std::span<const CorpusRoot> roots) {
  if (roots.empty()) return ScanFailure{ScanIssue::kInvalidRoot, {}, {}, {}};
  auto opened = ManifestStore::Open(manifest);
  if (const auto* error = std::get_if<ManifestError>(&opened)) {
    return ScanFailure{ScanIssue::kManifest, manifest, {}, *error};
  }
  auto store = std::move(std::get<std::unique_ptr<ManifestStore>>(opened));
  auto loaded = store->Load();
  if (const auto* error = std::get_if<ManifestError>(&loaded)) {
    return ScanFailure{ScanIssue::kManifest, manifest, {}, *error};
  }
  ScanStats stats;
  std::vector<InputFile> inputs;
  std::set<std::string> selected;
  if (const auto error = Collect(roots, &inputs, &selected, &stats)) return *error;
  std::vector<CorpusEntry> output;
  std::map<Sha256, CachedDetection> cache;
  for (auto& entry : std::get<std::vector<CorpusEntry>>(loaded)) {
    if (entry.detector_revision == kDetectorRevision) {
      cache.emplace(entry.sha256, CachedDetection{entry.size, entry.detection});
    }
    if (!selected.contains(entry.corpus_id)) output.push_back(std::move(entry));
  }
  if (inputs.size() > kMaxManifestEntries - output.size()) {
    return ScanFailure{ScanIssue::kLimit, manifest, {}, {}};
  }
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  if (now < 0) return ScanFailure{ScanIssue::kClock, {}, {}, {}};
  std::set<Sha256> unique;
  for (const auto& input : inputs) {
    auto file_result = AbcFile::Open(input.absolute);
    if (const auto* error = std::get_if<FileError>(&file_result)) {
      return ScanFailure{ScanIssue::kFile, input.absolute, *error, {}};
    }
    auto file = std::move(std::get<std::unique_ptr<AbcFile>>(file_result));
    auto fingerprint_result = file->Fingerprint();
    if (const auto* error = std::get_if<FileError>(&fingerprint_result)) {
      return ScanFailure{ScanIssue::kFile, input.absolute, *error, {}};
    }
    const auto& fingerprint = std::get<FileFingerprint>(fingerprint_result);
    if (fingerprint.size > UINT64_MAX - stats.bytes_hashed) {
      return ScanFailure{ScanIssue::kLimit, input.absolute, {}, {}};
    }
    stats.bytes_hashed += fingerprint.size;
    unique.insert(fingerprint.sha256);
    CorpusEntry entry;
    entry.corpus_id = input.corpus_id;
    entry.relative_path = input.relative;
    entry.size = fingerprint.size;
    entry.sha256 = fingerprint.sha256;
    entry.checked_at = now;
    const auto cached = cache.find(fingerprint.sha256);
    if (cached != cache.end() && cached->second.size == fingerprint.size) {
      entry.detection = cached->second.detection;
      ++stats.reused;
    } else {
      auto detected = DetectAbc(*file, fingerprint);
      if (const auto* error = std::get_if<FileError>(&detected)) {
        return ScanFailure{ScanIssue::kFile, input.absolute, *error, {}};
      }
      entry.detection = std::get<Detection>(detected);
      cache.insert_or_assign(entry.sha256, CachedDetection{entry.size, entry.detection});
      ++stats.classified;
    }
    if (const auto error = file->CheckUnchanged()) {
      return ScanFailure{ScanIssue::kFile, input.absolute, *error, {}};
    }
    CountKind(entry.detection.kind, &stats);
    output.push_back(std::move(entry));
    ++stats.files;
  }
  stats.unique_contents = unique.size();
  std::sort(output.begin(), output.end(), [](const auto& left, const auto& right) {
    return std::tie(left.corpus_id, left.relative_path) <
           std::tie(right.corpus_id, right.relative_path);
  });
  if (const auto error = store->Save(output)) {
    return ScanFailure{ScanIssue::kManifest, manifest, {}, *error};
  }
  return stats;
}
}  // namespace abcd::corpus
