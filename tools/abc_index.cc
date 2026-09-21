#include <sys/resource.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "abcd/abc/metadata_index.h"
#include "abcd/corpus/manifest.h"

namespace {
using abcd::corpus::AbcFile;
using abcd::corpus::CorpusEntry;
using abcd::corpus::FileError;
using abcd::corpus::FileFingerprint;
using abcd::corpus::ManifestError;
using abcd::corpus::ManifestStore;
using abcd::corpus::SampleKind;

constexpr rlim_t kProcessMemoryLimit = 1000000000;
// Only one index is alive at a time. Leave the rest of the process ceiling for
// IO, the manifest, output, allocator overhead and transient allocations.
constexpr std::size_t kIndexMemoryBudget = 256 * 1024 * 1024;
constexpr std::uint64_t kBytesPerKibibyte = 1024;
constexpr int kRequiredArguments = 4;
constexpr int kRootArgumentWidth = 2;

enum class IndexOutcome {
  kIndexed,
  kStaticSkipped,
  kUnknown,
  kInvalid,
  kStaleManifest,
  kFileFailure,
  kHeaderFailure,
  kMetadataFailure,
};

const char* OutcomeLabel(IndexOutcome outcome) {
  switch (outcome) {
    case IndexOutcome::kIndexed:
      return "indexed";
    case IndexOutcome::kStaticSkipped:
      return "static_skipped";
    case IndexOutcome::kUnknown:
      return "unknown";
    case IndexOutcome::kInvalid:
      return "invalid";
    case IndexOutcome::kStaleManifest:
      return "stale_manifest";
    case IndexOutcome::kFileFailure:
      return "file_failure";
    case IndexOutcome::kHeaderFailure:
      return "header_failure";
    case IndexOutcome::kMetadataFailure:
      return "metadata_failure";
  }
  std::abort();
}

struct FileReport {
  IndexOutcome outcome = IndexOutcome::kIndexed;
  std::uint64_t records = 0;
  std::uint64_t methods = 0;
  std::uint64_t code_methods = 0;
  std::uint64_t instruction_bytes = 0;
  std::optional<FileError> file_error;
  std::optional<abcd::abc::HeaderError> header_error;
  std::optional<abcd::abc::MetadataError> metadata_error;
};

FileReport IndexFile(const std::filesystem::path& root,
                     const CorpusEntry& entry) {
  FileReport report;
  auto opened = AbcFile::OpenBeneath(root, entry.relative_path);
  if (const auto* error = std::get_if<FileError>(&opened)) {
    report.outcome = IndexOutcome::kFileFailure;
    report.file_error = *error;
    return report;
  }
  const auto file = std::move(std::get<std::unique_ptr<AbcFile>>(opened));
  auto hashed = file->Fingerprint();
  if (const auto* error = std::get_if<FileError>(&hashed)) {
    report.outcome = IndexOutcome::kFileFailure;
    report.file_error = *error;
    return report;
  }
  const auto& fingerprint = std::get<FileFingerprint>(hashed);
  // Persisted classifications are reusable only for identical bytes and rules.
  // In particular, decode failure must never be reclassified as static ABC.
  if (fingerprint.sha256 != entry.sha256 || fingerprint.size != entry.size ||
      entry.detector_revision != abcd::corpus::kDetectorRevision) {
    report.outcome = IndexOutcome::kStaleManifest;
    return report;
  }
  switch (entry.detection.kind) {
    case SampleKind::kStatic:
      report.outcome = IndexOutcome::kStaticSkipped;
      return report;
    case SampleKind::kUnknown:
      report.outcome = IndexOutcome::kUnknown;
      return report;
    case SampleKind::kInvalid:
      report.outcome = IndexOutcome::kInvalid;
      return report;
    case SampleKind::kDynamic:
      break;
  }
  auto parsed = abcd::abc::ParseHeader(
      std::span(fingerprint.prefix).first(fingerprint.prefix_size),
      fingerprint.size);
  if (const auto* error = std::get_if<abcd::abc::HeaderError>(&parsed)) {
    report.outcome = IndexOutcome::kHeaderFailure;
    report.header_error = *error;
    return report;
  }
  const auto& header = std::get<abcd::abc::Header>(parsed);
  if (header.kind != abcd::abc::AbcKind::kDynamic ||
      header.profile != entry.detection.profile ||
      header.version != entry.detection.version ||
      header.checksum != fingerprint.adler32) {
    report.outcome = IndexOutcome::kStaleManifest;
    return report;
  }
  const auto indexed = abcd::abc::BuildMetadataIndex(
      *file, header, abcd::abc::MetadataBudget{kIndexMemoryBudget});
  if (const auto* error = std::get_if<abcd::abc::MetadataError>(&indexed)) {
    report.outcome = IndexOutcome::kMetadataFailure;
    report.metadata_error = *error;
    return report;
  }
  const auto& index = std::get<abcd::abc::MetadataIndex>(indexed);
  report.records = index.records.size();
  report.methods = index.methods.size();
  for (const auto& method : index.methods) {
    if (!method.code) continue;
    ++report.code_methods;
    report.instruction_bytes += method.code->instruction_bytes;
  }
  return report;
}

nlohmann::json Serialize(const CorpusEntry& entry, const FileReport& report) {
  nlohmann::json result{{"corpus_id", entry.corpus_id},
                        {"path", entry.relative_path.generic_string()},
                        {"outcome", OutcomeLabel(report.outcome)},
                        {"records", report.records},
                        {"methods", report.methods},
                        {"code_methods", report.code_methods},
                        {"instruction_bytes", report.instruction_bytes}};
  if (report.file_error)
    result["file_error"] = static_cast<int>(*report.file_error);
  if (report.header_error)
    result["header_error"] = static_cast<int>(*report.header_error);
  if (report.metadata_error)
    result["metadata_error"] = static_cast<int>(*report.metadata_error);
  return result;
}

int Run(int argc, char** argv) {
  if (argc < kRequiredArguments || (argc - 2) % kRootArgumentWidth != 0) {
    std::cerr
        << "usage: abc_index MANIFEST CORPUS_ID ROOT [CORPUS_ID ROOT ...]\n";
    return EXIT_FAILURE;
  }
  rlimit limit{};
  if (getrlimit(RLIMIT_AS, &limit) != 0) return EXIT_FAILURE;
  limit.rlim_cur = std::min(limit.rlim_cur, kProcessMemoryLimit);
  if (setrlimit(RLIMIT_AS, &limit) != 0) return EXIT_FAILURE;

  std::map<std::string, std::filesystem::path> roots;
  for (int i = 2; i < argc; i += kRootArgumentWidth) {
    std::error_code error;
    const auto root = std::filesystem::canonical(argv[i + 1], error);
    if (error || !std::filesystem::is_directory(root, error) || error ||
        std::string_view(argv[i]).empty() ||
        !roots.emplace(argv[i], root).second) {
      std::cerr << "invalid or duplicate corpus root: " << argv[i] << '\n';
      return EXIT_FAILURE;
    }
  }
  auto opened = ManifestStore::Open(argv[1]);
  if (const auto* error = std::get_if<ManifestError>(&opened)) {
    std::cerr << "manifest open failed: " << static_cast<int>(*error) << '\n';
    return EXIT_FAILURE;
  }
  const auto store =
      std::move(std::get<std::unique_ptr<ManifestStore>>(opened));
  auto loaded = store->Load();
  if (const auto* error = std::get_if<ManifestError>(&loaded)) {
    std::cerr << "manifest load failed: " << static_cast<int>(*error) << '\n';
    return EXIT_FAILURE;
  }
  std::set<std::string> present_roots;
  for (const auto& entry : std::get<std::vector<CorpusEntry>>(loaded))
    present_roots.insert(entry.corpus_id);
  for (const auto& [id, path] : roots) {
    if (!present_roots.contains(id)) {
      std::cerr << "corpus root absent from manifest: " << id << '\n';
      return EXIT_FAILURE;
    }
  }
  std::uint64_t files = 0, indexed = 0, skipped = 0, failed = 0;
  std::uint64_t records = 0, methods = 0, code_methods = 0;
  std::uint64_t instruction_bytes = 0;
  auto reports = nlohmann::json::array();
  for (const auto& entry : std::get<std::vector<CorpusEntry>>(loaded)) {
    const auto root = roots.find(entry.corpus_id);
    if (root == roots.end()) continue;
    ++files;
    const auto report = IndexFile(root->second, entry);
    switch (report.outcome) {
      case IndexOutcome::kIndexed:
        ++indexed;
        break;
      case IndexOutcome::kStaticSkipped:
        ++skipped;
        break;
      case IndexOutcome::kUnknown:
      case IndexOutcome::kInvalid:
      case IndexOutcome::kStaleManifest:
      case IndexOutcome::kFileFailure:
      case IndexOutcome::kHeaderFailure:
      case IndexOutcome::kMetadataFailure:
        ++failed;
        break;
    }
    records += report.records;
    methods += report.methods;
    code_methods += report.code_methods;
    instruction_bytes += report.instruction_bytes;
    reports.push_back(Serialize(entry, report));
  }
  // Stream the potentially large results before measuring RSS; do not create
  // a second full-size JSON string merely to print the report.
  std::cout << "{\"results\":" << reports;
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return EXIT_FAILURE;
  const nlohmann::json summary{
      {"files", files},
      {"indexed", indexed},
      {"static_skipped", skipped},
      {"failed", failed},
      {"records", records},
      {"methods", methods},
      {"code_methods", code_methods},
      {"instruction_bytes", instruction_bytes},
      {"peak_rss_bytes",
       static_cast<std::uint64_t>(usage.ru_maxrss) * kBytesPerKibibyte},
      {"address_space_limit_bytes", limit.rlim_cur}};
  for (const auto& [key, value] : summary.items())
    std::cout << ',' << nlohmann::json(key) << ':' << value;
  std::cout << "}\n";
  return files != 0 && failed == 0 && std::cout.good() ? EXIT_SUCCESS
                                                       : EXIT_FAILURE;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    return Run(argc, argv);
  } catch (const std::bad_alloc&) {
    std::cerr << "process memory budget exhausted\n";
  } catch (const std::exception& error) {
    std::cerr << "index failed: " << error.what() << '\n';
  }
  return EXIT_FAILURE;
}
