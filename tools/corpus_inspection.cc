#include "corpus_inspection.h"


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
#include "process_limit.h"
#include "source_cli_options.h"
#include "abcd/corpus/manifest.h"
#include "method_inspection.h"
#include "module_inspection.h"

namespace abcd::tools {
namespace {
using abcd::corpus::AbcFile;
using abcd::corpus::CorpusEntry;
using abcd::corpus::FileError;
using abcd::corpus::FileFingerprint;
using abcd::corpus::ManifestError;
using abcd::corpus::ManifestStore;
using abcd::corpus::SampleKind;

// Only one index is alive at a time. Leave the rest of the process ceiling for
// IO, the manifest, output, allocator overhead and transient allocations.
constexpr std::size_t kIndexMemoryBudget = 256 * 1024 * 1024;
constexpr int kRequiredArguments = 4;
constexpr int kRootArgumentWidth = 2;

enum class IndexOutcome {
  kIndexed,
  kDecoded,
  kStaticSkipped,
  kUnknown,
  kInvalid,
  kStaleManifest,
  kFileFailure,
  kHeaderFailure,
  kMetadataFailure,
  kBodyFailure,
  kFlowBuilt,
  kFlowFailure,
  kDispatchBuilt,
  kDispatchFailure,
  kReferencesChecked,
  kReferenceFailure,
  kModulesChecked,
  kModuleFailure,
  kDominatorsBuilt,
  kDominatorFailure,
  kFramesChecked,
  kFrameFailure,
  kEnvironmentsProbed,
  kCreationsProbed,
  kCreationFailure,
};

const char* ToolName(InspectionStage stage) {
  switch (stage) {
    case InspectionStage::kMetadata:
      return "abc_index";
    case InspectionStage::kInstructions:
      return "abc_decode";
    case InspectionStage::kNormalFlow:
      return "abc_flow";
    case InspectionStage::kExceptionDispatch:
      return "abc_exceptions";
    case InspectionStage::kReferences:
      return "abc_references";
    case InspectionStage::kModules:
      return "abc_modules";
    case InspectionStage::kDominators:
      return "abc_dominators";
    case InspectionStage::kFrames:
      return "abc_frames";
    case InspectionStage::kEnvironments:
      return "abc_environments";
    case InspectionStage::kCreations:
      return "abc_creations";
  }
  std::abort();
}

const char* OutcomeLabel(IndexOutcome outcome) {
  switch (outcome) {
    case IndexOutcome::kIndexed:
      return "indexed";
    case IndexOutcome::kDecoded:
      return "decoded";
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
    case IndexOutcome::kBodyFailure:
      return "body_failure";
    case IndexOutcome::kFlowBuilt:
      return "flow_built";
    case IndexOutcome::kFlowFailure:
      return "flow_failure";
    case IndexOutcome::kDispatchBuilt:
      return "dispatch_built";
    case IndexOutcome::kDispatchFailure:
      return "dispatch_failure";
    case IndexOutcome::kReferencesChecked:
      return "references_checked";
    case IndexOutcome::kReferenceFailure:
      return "reference_failure";
    case IndexOutcome::kModulesChecked:
      return "modules_checked";
    case IndexOutcome::kModuleFailure:
      return "module_failure";
    case IndexOutcome::kDominatorsBuilt:
      return "dominators_built";
    case IndexOutcome::kDominatorFailure:
      return "dominator_failure";
    case IndexOutcome::kFramesChecked:
      return "frames_checked";
    case IndexOutcome::kFrameFailure:
      return "frame_failure";
    case IndexOutcome::kEnvironmentsProbed:
      return "environments_probed";
    case IndexOutcome::kCreationsProbed:
      return "creations_probed";
    case IndexOutcome::kCreationFailure:
      return "creation_failure";
  }
  std::abort();
}

struct FileReport {
  IndexOutcome outcome = IndexOutcome::kIndexed;
  std::uint64_t records = 0;
  std::uint64_t methods = 0;
  std::uint64_t fields = 0;
  std::optional<FileError> file_error;
  std::optional<abcd::abc::HeaderError> header_error;
  std::optional<abcd::abc::MetadataError> metadata_error;
  MethodCounts inspection;
  ModuleCounts modules;
};

FileReport IndexFile(const std::filesystem::path& root,
                     const CorpusEntry& entry, InspectionStage stage,
                     std::ostream& diagnostics,
                     std::optional<std::string_view> bundle_descriptor) {
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
  report.fields = index.fields.size();
  if (stage == InspectionStage::kModules) {
    report.modules = InspectModules(*file, header, index, diagnostics, bundle_descriptor);
    report.outcome = report.modules.failed_module_records
                         ? IndexOutcome::kModuleFailure : IndexOutcome::kModulesChecked;
    return report;
  }
  report.inspection =
      InspectMethods(*file, fingerprint.sha256, header, index, stage, diagnostics);
  // A file reports the earliest failed stage, independent of method order.
  // Per-stage totals and every diagnostic remain available for mixed failures.
  if (report.inspection.failed_creation_indexes != 0) {
    report.outcome = IndexOutcome::kCreationFailure;
  } else if (report.inspection.failed_frame_methods != 0) {
    report.outcome = IndexOutcome::kFrameFailure;
  } else if (report.inspection.failed_methods != 0) {
    report.outcome = IndexOutcome::kBodyFailure;
  } else if (report.inspection.failed_flow_methods != 0) {
    report.outcome = IndexOutcome::kFlowFailure;
  } else if (report.inspection.failed_dominator_methods != 0) {
    report.outcome = IndexOutcome::kDominatorFailure;
  } else if (report.inspection.failed_dispatch_methods != 0) {
    report.outcome = IndexOutcome::kDispatchFailure;
  } else if (report.inspection.failed_reference_methods != 0) {
    report.outcome = IndexOutcome::kReferenceFailure;
  } else {
    switch (stage) {
      case InspectionStage::kMetadata:
        report.outcome = IndexOutcome::kIndexed;
        break;
      case InspectionStage::kInstructions:
        report.outcome = IndexOutcome::kDecoded;
        break;
      case InspectionStage::kNormalFlow:
        report.outcome = IndexOutcome::kFlowBuilt;
        break;
      case InspectionStage::kExceptionDispatch:
        report.outcome = IndexOutcome::kDispatchBuilt;
        break;
      case InspectionStage::kReferences:
        report.outcome = IndexOutcome::kReferencesChecked;
        break;
      case InspectionStage::kModules:
        std::abort();  // Handled before method inspection.
      case InspectionStage::kDominators:
        report.outcome = IndexOutcome::kDominatorsBuilt;
        break;
      case InspectionStage::kFrames:
        report.outcome = IndexOutcome::kFramesChecked;
        break;
      case InspectionStage::kEnvironments:
        report.outcome = IndexOutcome::kEnvironmentsProbed;
        break;
      case InspectionStage::kCreations:
        report.outcome = IndexOutcome::kCreationsProbed;
        break;
    }
  }
  return report;
}

nlohmann::json Serialize(const CorpusEntry& entry, const FileReport& report) {
  nlohmann::json result{{"corpus_id", entry.corpus_id},
                        {"path", entry.relative_path.generic_string()},
                        {"outcome", OutcomeLabel(report.outcome)},
                        {"records", report.records},
                        {"methods", report.methods},
                        {"fields", report.fields}};
  if (report.file_error)
    result["file_error"] = static_cast<int>(*report.file_error);
  if (report.header_error)
    result["header_error"] = static_cast<int>(*report.header_error);
  if (report.metadata_error)
    result["metadata_error"] = static_cast<int>(*report.metadata_error);
  result.update(SerializeCounts(report.inspection));
  if (report.outcome == IndexOutcome::kModulesChecked || report.outcome == IndexOutcome::kModuleFailure)
    result.update(SerializeCounts(report.modules));
  return result;
}

int Run(int argc, char** argv, InspectionStage stage) {
  std::optional<std::string_view> bundle_descriptor;
  constexpr int kDescriptorArguments = 2;
  if (stage == InspectionStage::kModules && argc > kDescriptorArguments &&
      std::string_view(argv[1]) == "--bundle-descriptor") {
    bundle_descriptor = argv[2];
    argc -= kDescriptorArguments;
    argv += kDescriptorArguments;
  }
  if (argc < kRequiredArguments || (argc - 2) % kRootArgumentWidth != 0) {
    std::cerr << "usage: " << ToolName(stage)
              << " [--bundle-descriptor VALUE (modules only)] MANIFEST CORPUS_ID ROOT [CORPUS_ID ROOT ...]\n";
    return EXIT_FAILURE;
  }
  const auto limit = EstablishProcessLimit();
  if (!ProcessLimitAcceptable(limit)) {
    ReportLimitFailure(std::cerr, limit);
    return EXIT_FAILURE;
  }

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
  std::uint64_t records = 0, methods = 0, fields = 0;
  ModuleCounts modules;
  MethodCounts inspection;
  std::cout << "{\"results\":[";
  for (const auto& entry : std::get<std::vector<CorpusEntry>>(loaded)) {
    const auto root = roots.find(entry.corpus_id);
    if (root == roots.end()) continue;
    if (files != 0) std::cout << ',';
    ++files;
    std::cout << (stage == InspectionStage::kModules ? "{\"module_failures\":[" : "{\"method_failures\":[");
    const auto report = IndexFile(root->second, entry, stage, std::cout, bundle_descriptor);
    std::cout << ']';
    const auto serialized = Serialize(entry, report);
    for (const auto& [key, value] : serialized.items())
      std::cout << ',' << nlohmann::json(key) << ':' << value;
    std::cout << '}';
    if (!std::cout.good()) return EXIT_FAILURE;
    switch (report.outcome) {
      case IndexOutcome::kIndexed:
      case IndexOutcome::kDecoded:
      case IndexOutcome::kFlowBuilt:
      case IndexOutcome::kDispatchBuilt:
      case IndexOutcome::kReferencesChecked:
      case IndexOutcome::kModulesChecked:
      case IndexOutcome::kDominatorsBuilt:
      case IndexOutcome::kFramesChecked:
      case IndexOutcome::kEnvironmentsProbed:
      case IndexOutcome::kCreationsProbed:
        ++indexed;
        break;
      case IndexOutcome::kStaticSkipped:
        ++skipped;
        break;
      case IndexOutcome::kBodyFailure:
      case IndexOutcome::kFlowFailure:
      case IndexOutcome::kDispatchFailure:
      case IndexOutcome::kReferenceFailure:
      case IndexOutcome::kModuleFailure:
      case IndexOutcome::kDominatorFailure:
      case IndexOutcome::kFrameFailure:
      case IndexOutcome::kCreationFailure:
        ++indexed;
        ++failed;
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
    fields += report.fields;
    inspection += report.inspection;
    modules += report.modules;
  }
  // Per-file reports and failures are already streamed, with no full-corpus
  // report buffer. Include output allocations in the measured peak.
  std::cout << ']';
  nlohmann::json summary{
      {"stage", StageLabel(stage)},
      {"files", files},
      {"indexed", indexed},
      {"static_skipped", skipped},
      {"failed", failed},
      {"records", records},
      {"methods", methods},
      {"fields", fields}};
  ReportProcessMemory(summary, limit);
  summary.update(SerializeCounts(inspection));
  if (bundle_descriptor) summary["bundle_descriptor"] = *bundle_descriptor;
  if (stage == InspectionStage::kModules) summary.update(SerializeCounts(modules));
  for (const auto& [key, value] : summary.items())
    std::cout << ',' << nlohmann::json(key) << ':' << value;
  std::cout << "}\n";
  return files != 0 && failed == 0 && std::cout.good() ? EXIT_SUCCESS
                                                       : EXIT_FAILURE;
}
}  // namespace

int RunCorpusInspection(int argc, char** argv, InspectionStage stage) {
  if (const auto removed = abcd::tools::FindRemovedSourceOption(argc, argv)) {
    abcd::tools::ReportRemovedSourceOption(std::cerr, *removed);
    return EXIT_FAILURE;
  }
  try {
    return Run(argc, argv, stage);
  } catch (const std::bad_alloc&) {
    std::cerr << "process memory budget exhausted\n";
  } catch (const std::exception& error) {
    std::cerr << "corpus inspection failed: " << error.what() << '\n';
  }
  return EXIT_FAILURE;
}
}  // namespace abcd::tools
