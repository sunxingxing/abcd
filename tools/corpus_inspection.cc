#include "corpus_inspection.h"

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
#include "abcd/abc/method_body.h"
#include "abcd/analysis/normal_flow.h"
#include "abcd/corpus/manifest.h"

namespace abcd::tools {
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
};

const char* StageLabel(InspectionStage stage) {
  switch (stage) {
    case InspectionStage::kMetadata:
      return "metadata";
    case InspectionStage::kInstructions:
      return "method_encoding";
    case InspectionStage::kNormalFlow:
      return "normal_flow";
  }
  std::abort();
}

const char* ToolName(InspectionStage stage) {
  switch (stage) {
    case InspectionStage::kMetadata:
      return "abc_index";
    case InspectionStage::kInstructions:
      return "abc_decode";
    case InspectionStage::kNormalFlow:
      return "abc_flow";
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
  }
  std::abort();
}

void WriteFailure(std::ostream& output, const abc::MethodInfo& method,
                  const abc::BodyFailure& failure) {
  nlohmann::json diagnostic{{"method_offset", method.offset.value},
                            {"code_offset", method.code->offset.value},
                            {"pc", failure.offset.value},
                            {"kind", static_cast<int>(failure.kind)}};
  diagnostic["stage"] = StageLabel(InspectionStage::kInstructions);
  if (failure.instruction_error)
    diagnostic["instruction_error"] =
        static_cast<int>(*failure.instruction_error);
  if (failure.file_error)
    diagnostic["file_error"] = static_cast<int>(*failure.file_error);
  output << diagnostic;
}

void WriteFailure(std::ostream& output, const abc::MethodInfo& method,
                  const analysis::FlowFailure& failure) {
  output << nlohmann::json{{"method_offset", method.offset.value},
                           {"code_offset", method.code->offset.value},
                           {"pc", failure.offset.value},
                           {"kind", static_cast<int>(failure.kind)},
                           {"stage", StageLabel(InspectionStage::kNormalFlow)}};
}

struct DecodeCounts {
  std::uint64_t decoded_methods = 0;
  std::uint64_t failed_methods = 0;
  std::uint64_t instructions = 0;
  std::uint64_t try_regions = 0;
  std::uint64_t catch_handlers = 0;
  std::uint64_t flow_methods = 0;
  std::uint64_t failed_flow_methods = 0;
  std::uint64_t blocks = 0;
  std::uint64_t edges = 0;
  std::uint64_t resume_edges = 0;
};

struct FileReport {
  IndexOutcome outcome = IndexOutcome::kIndexed;
  std::uint64_t records = 0;
  std::uint64_t methods = 0;
  std::uint64_t code_methods = 0;
  std::uint64_t instruction_bytes = 0;
  std::optional<FileError> file_error;
  std::optional<abcd::abc::HeaderError> header_error;
  std::optional<abcd::abc::MetadataError> metadata_error;
  DecodeCounts decoded;
};

FileReport IndexFile(const std::filesystem::path& root,
                     const CorpusEntry& entry, InspectionStage stage,
                     std::ostream& diagnostics) {
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
    if (stage != InspectionStage::kMetadata) {
      const auto decoded =
          abc::DecodeMethodBody(*file, header.profile, *method.code);
      if (const auto* failure = std::get_if<abc::BodyFailure>(&decoded)) {
        report.outcome = IndexOutcome::kBodyFailure;
        // Stream every failure. Keeping a capped vector loses method evidence;
        // retaining all failures would let malformed input exhaust the budget.
        if (report.decoded.failed_methods +
                report.decoded.failed_flow_methods !=
            0)
          diagnostics << ',';
        WriteFailure(diagnostics, method, *failure);
        ++report.decoded.failed_methods;
      } else {
        const auto& body = std::get<abc::MethodBody>(decoded);
        ++report.decoded.decoded_methods;
        report.decoded.instructions += body.instructions.size();
        report.decoded.try_regions += body.tries.size();
        report.decoded.catch_handlers += body.handlers.size();
        if (stage == InspectionStage::kNormalFlow) {
          const auto flow = analysis::BuildNormalFlow(body);
          if (const auto* failure = std::get_if<analysis::FlowFailure>(&flow)) {
            report.outcome = IndexOutcome::kFlowFailure;
            if (report.decoded.failed_methods +
                    report.decoded.failed_flow_methods !=
                0)
              diagnostics << ',';
            WriteFailure(diagnostics, method, *failure);
            ++report.decoded.failed_flow_methods;
          } else {
            ++report.decoded.flow_methods;
            const auto& graph = std::get<analysis::NormalFlow>(flow);
            report.decoded.blocks += graph.blocks.size();
            for (const auto& block : graph.blocks) {
              report.decoded.edges += block.successor_count;
              for (const auto& edge :
                   std::span(block.successors).first(block.successor_count))
                if (edge.kind == analysis::FlowEdgeKind::kResume)
                  ++report.decoded.resume_edges;
            }
          }
        }
      }
    }
  }
  if (stage == InspectionStage::kInstructions &&
      report.decoded.failed_methods == 0)
    report.outcome = IndexOutcome::kDecoded;
  if (stage == InspectionStage::kNormalFlow &&
      report.decoded.failed_methods == 0 &&
      report.decoded.failed_flow_methods == 0)
    report.outcome = IndexOutcome::kFlowBuilt;
  return report;
}

nlohmann::json SerializeCounts(const DecodeCounts& counts) {
  return {{"decoded_methods", counts.decoded_methods},
          {"failed_methods", counts.failed_methods},
          {"instructions", counts.instructions},
          {"try_regions", counts.try_regions},
          {"catch_handlers", counts.catch_handlers},
          {"flow_methods", counts.flow_methods},
          {"failed_flow_methods", counts.failed_flow_methods},
          {"blocks", counts.blocks},
          {"edges", counts.edges},
          {"resume_edges", counts.resume_edges}};
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
  result.update(SerializeCounts(report.decoded));
  return result;
}

int Run(int argc, char** argv, InspectionStage stage) {
  if (argc < kRequiredArguments || (argc - 2) % kRootArgumentWidth != 0) {
    std::cerr << "usage: " << ToolName(stage)
              << " MANIFEST CORPUS_ID ROOT [CORPUS_ID ROOT ...]\n";
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
  DecodeCounts decoded;
  std::cout << "{\"results\":[";
  for (const auto& entry : std::get<std::vector<CorpusEntry>>(loaded)) {
    const auto root = roots.find(entry.corpus_id);
    if (root == roots.end()) continue;
    if (files != 0) std::cout << ',';
    ++files;
    std::cout << "{\"method_failures\":[";
    const auto report = IndexFile(root->second, entry, stage, std::cout);
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
        ++indexed;
        break;
      case IndexOutcome::kStaticSkipped:
        ++skipped;
        break;
      case IndexOutcome::kBodyFailure:
      case IndexOutcome::kFlowFailure:
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
    code_methods += report.code_methods;
    instruction_bytes += report.instruction_bytes;
    decoded.decoded_methods += report.decoded.decoded_methods;
    decoded.failed_methods += report.decoded.failed_methods;
    decoded.instructions += report.decoded.instructions;
    decoded.try_regions += report.decoded.try_regions;
    decoded.catch_handlers += report.decoded.catch_handlers;
    decoded.flow_methods += report.decoded.flow_methods;
    decoded.failed_flow_methods += report.decoded.failed_flow_methods;
    decoded.blocks += report.decoded.blocks;
    decoded.edges += report.decoded.edges;
    decoded.resume_edges += report.decoded.resume_edges;
  }
  // Per-file reports and failures are already streamed, with no full-corpus
  // report buffer. Include output allocations in the measured peak.
  std::cout << ']';
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return EXIT_FAILURE;
  nlohmann::json summary{
      {"stage", StageLabel(stage)},
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
  summary.update(SerializeCounts(decoded));
  for (const auto& [key, value] : summary.items())
    std::cout << ',' << nlohmann::json(key) << ':' << value;
  std::cout << "}\n";
  return files != 0 && failed == 0 && std::cout.good() ? EXIT_SUCCESS
                                                       : EXIT_FAILURE;
}
}  // namespace

int RunCorpusInspection(int argc, char** argv, InspectionStage stage) {
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
