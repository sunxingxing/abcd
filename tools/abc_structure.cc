#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

#include "abcd/corpus/detect.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/record_program.h"
#include "abcd/structure/record_path.h"
#include "abcd/structure/record_writer.h"

namespace {
using namespace abcd;
enum class ExportError {
  kOpen,
  kHash,
  kKind,
  kHeader,
  kIndex,
  kPackaging,
  kPath,
  kRecord,
  kOutput,
  kChanged,
  kDestination,
  kRoles
};
enum class SourceStatus : unsigned {
  kWritten = 0,
  kModuleContext = 1,
  kNoModule = 2,
  kProgram = 3
};
bool FatalMetadata(abc::MetadataError error) {
  return error == abc::MetadataError::kResourceLimit ||
         error == abc::MetadataError::kRead ||
         error == abc::MetadataError::kFileChanged;
}
bool FatalContext(const source::RecordModuleFailure& failure) {
  if (const auto* error = std::get_if<source::RecordModuleError>(&failure.reason))
    return *error == source::RecordModuleError::kResourceLimit;
  if (const auto* error = std::get_if<abc::AttributeFailure>(&failure.reason))
    return error->kind == abc::AttributeError::kFileChanged ||
           (error->metadata_error && FatalMetadata(*error->metadata_error));
  const auto kind = std::get<abc::ModuleFailure>(failure.reason).kind;
  return kind == abc::ModuleError::kFileChanged ||
         kind == abc::ModuleError::kRead ||
         kind == abc::ModuleError::kResourceLimit;
}
int Fail(ExportError kind, std::uint32_t entity = 0) {
  std::cerr << nlohmann::json{{"error", static_cast<unsigned>(kind)},
                              {"entity", entity}}
            << '\n';
  return EXIT_FAILURE;
}
class StagedTree {
 public:
  explicit StagedTree(const std::filesystem::path& destination) {
    auto pattern = destination.string() + ".partial.XXXXXX";
    if (mkdtemp(pattern.data())) path = pattern;
  }
  ~StagedTree() {
    if (!path.empty()) {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  }
  bool Publish(const std::filesystem::path& destination) {
    // Linux no-replace rename: a racing pre-existing destination, including a
    // symlink, cannot be overwritten. All children were made in our fresh tree.
    if (renameat2(AT_FDCWD, path.c_str(), AT_FDCWD, destination.c_str(),
                  RENAME_NOREPLACE) != 0)
      return false;
    path.clear();
    return true;
  }
  std::filesystem::path path;
};
std::string Hex(const corpus::Sha256& hash) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result;
  result.reserve(hash.bytes.size() * 2);
  for (const auto byte : hash.bytes) {
    result += digits[byte >> 4];
    result += digits[byte & 0xf];
  }
  return result;
}
int Export(const std::filesystem::path& input,
           const std::filesystem::path& output, bool emit_source) {
  if (output.filename().empty() ||
      std::filesystem::exists(std::filesystem::symlink_status(output)))
    return Fail(ExportError::kDestination);
  auto opened = corpus::AbcFile::Open(input);
  const auto* owned = std::get_if<std::unique_ptr<corpus::AbcFile>>(&opened);
  if (!owned) return Fail(ExportError::kOpen);
  const auto& file = **owned;
  const auto fingerprint = file.Fingerprint();
  const auto* hash = std::get_if<corpus::FileFingerprint>(&fingerprint);
  if (!hash) return Fail(ExportError::kHash);
  const auto detected = corpus::DetectAbc(file, *hash);
  const auto* detection = std::get_if<corpus::Detection>(&detected);
  if (!detection || detection->kind != corpus::SampleKind::kDynamic)
    return Fail(ExportError::kKind);
  const auto parsed = abc::ParseHeader(
      std::span(hash->prefix).first(hash->prefix_size), hash->size);
  const auto* header = std::get_if<abc::Header>(&parsed);
  if (!header || header->checksum != hash->adler32)
    return Fail(ExportError::kHeader);
  constexpr std::size_t kIndexBytes = 256 * 1024 * 1024;
  const auto indexed = abc::BuildMetadataIndex(file, *header, {kIndexBytes});
  const auto* index = std::get_if<abc::MetadataIndex>(&indexed);
  if (!index) return Fail(ExportError::kIndex);
  const auto looked_up = abc::MethodLookup::Build(index->methods);
  const auto* lookup = std::get_if<abc::MethodLookup>(&looked_up);
  if (!lookup) return Fail(ExportError::kIndex);
  const auto packed = abc::DetectFilePackaging(file, *header, *index);
  const auto* packaging = std::get_if<abc::FilePackaging>(&packed);
  if (!packaging) return Fail(ExportError::kPackaging);
  std::optional<analysis::MethodRoleIndex> roles;
  if (emit_source && source::SupportsRecordProgramProfile(header->profile)) {
    auto built =
        analysis::MethodRoleIndex::Build(file, *header, *index, *lookup);
    if (auto* ready = std::get_if<analysis::MethodRoleIndex>(&built))
      roles.emplace(std::move(*ready));
    else
      return Fail(ExportError::kRoles);
  }
  std::vector<structure::RecordPath> source_paths;
  if (emit_source) {
    auto planned = structure::PlanRecordSourcePaths(file, *header, *index);
    if (const auto* error = std::get_if<structure::PathError>(&planned)) {
      std::cerr << nlohmann::json{{"path_error", static_cast<unsigned>(*error)}}
                << '\n';
      return Fail(ExportError::kPath);
    }
    source_paths = std::get<std::vector<structure::RecordPath>>(std::move(planned));
  }
  const auto parent = output.parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent);
  StagedTree tree(output);
  if (tree.path.empty()) return Fail(ExportError::kOutput);
  std::ofstream manifest(tree.path / "manifest.json", std::ios::binary);
  manifest << "{\"schema\":1,\"view\":\""
           << (emit_source ? "declarations_with_verified_source"
                           : "declarations_only")
           << "\",\"sha256\":\"" << Hex(hash->sha256)
           << "\",\"packaging\":" << static_cast<unsigned>(*packaging)
           << ",\"records\":[";
  structure::RecordCounts totals;
  bool comma = false;
  std::size_t record_index = 0;
  for (const auto& record : index->records) {
    const auto planned = structure::PlanRecordPath(record);
    const auto* path = std::get_if<structure::RecordPath>(&planned);
    if (!path) return Fail(ExportError::kPath, record.offset.value);
    const auto stem = std::filesystem::path("records") / path->stem;
    std::filesystem::create_directories((tree.path / stem).parent_path());
    const auto metadata_path = stem.string() + ".record.json";
    const auto declaration_path = stem.string() + ".d.ts";
    std::ofstream metadata(tree.path / metadata_path, std::ios::binary);
    std::ofstream declarations(tree.path / declaration_path, std::ios::binary);
    if (!metadata || !declarations)
      return Fail(ExportError::kOutput, record.offset.value);
    const auto result =
        structure::WriteRecordStructure(file, *header, *index, *lookup, record,
                                        *packaging, metadata, declarations);
    const auto* counts = std::get_if<structure::RecordCounts>(&result);
    if (!counts) {
      const auto& failure = std::get<structure::StructureFailure>(result);
      std::cerr << nlohmann::json{{"failure_variant", failure.error.index()},
                                  {"entity", failure.entity.value}}
                << '\n';
      return Fail(ExportError::kRecord, record.offset.value);
    }
    metadata.close();
    declarations.close();
    if (!metadata || !declarations)
      return Fail(ExportError::kOutput, record.offset.value);
    totals.methods += counts->methods;
    totals.fields += counts->fields;
    totals.requests += counts->requests;
    totals.imports += counts->imports;
    totals.exports += counts->exports;
    nlohmann::json record_manifest{{"entity", record.offset.value},
                                   {"metadata", metadata_path},
                                   {"declarations", declaration_path}};
    if (emit_source) {
      const auto source_stem = std::filesystem::path("sources") /
                               source_paths[record_index].stem;
      const auto source_path = source_stem.string() + ".mjs";
      record_manifest["planned_source"] = source_path;
      record_manifest["source_path_origin"] =
          static_cast<unsigned>(source_paths[record_index].origin);
      if (!roles) {
        record_manifest["source_status"] =
            static_cast<unsigned>(SourceStatus::kProgram);
        record_manifest["source_error"] =
            static_cast<unsigned>(source::RecordProgramError::kProfile);
      } else {
        const auto context = source::ReadRecordModuleContext(
            file, *header, *index, record, *packaging);
        if (const auto* ready =
                std::get_if<source::RecordModuleContext>(&context)) {
          const auto module = ready->ForOwner(file, record.offset);
          if (!module) {
            record_manifest["source_status"] =
                static_cast<unsigned>(SourceStatus::kNoModule);
          } else {
            std::filesystem::create_directories(
                (tree.path / source_stem).parent_path());
            std::ofstream program(tree.path / source_path, std::ios::binary);
            if (!program) return Fail(ExportError::kOutput, record.offset.value);
            const auto written = source::WriteRecordProgram(
                file, *header, *index, *lookup, record, *module, *roles,
                program);
            program.close();
            if (!program) return Fail(ExportError::kOutput, record.offset.value);
            if (const auto* failure =
                    std::get_if<source::RecordProgramFailure>(&written)) {
              if (failure->kind == source::RecordProgramError::kWrite ||
                  failure->kind == source::RecordProgramError::kRead ||
                  failure->kind == source::RecordProgramError::kFileChanged ||
                  failure->kind == source::RecordProgramError::kResourceLimit)
                return Fail(ExportError::kRecord, record.offset.value);
              std::error_code removed;
              std::filesystem::remove(tree.path / source_path, removed);
              if (removed) return Fail(ExportError::kOutput, record.offset.value);
              record_manifest["source_status"] =
                  static_cast<unsigned>(SourceStatus::kProgram);
              record_manifest["source_error"] =
                  static_cast<unsigned>(failure->kind);
              record_manifest["source_pc"] = failure->pc.value;
            } else {
              record_manifest["source_status"] =
                  static_cast<unsigned>(SourceStatus::kWritten);
              record_manifest["source"] = source_path;
            }
          }
        } else {
          const auto& failure = std::get<source::RecordModuleFailure>(context);
          if (FatalContext(failure))
            return Fail(ExportError::kRecord, record.offset.value);
          record_manifest["source_status"] =
              static_cast<unsigned>(SourceStatus::kModuleContext);
          record_manifest["source_error"] = failure.reason.index();
        }
      }
    }
    if (comma) manifest << ',';
    comma = true;
    manifest << record_manifest;
    if (!manifest) return Fail(ExportError::kOutput);
    ++record_index;
  }
  const nlohmann::json counts{
      {"records", index->records.size()}, {"methods", totals.methods},
      {"record_fields", totals.fields},   {"module_requests", totals.requests},
      {"module_imports", totals.imports}, {"module_exports", totals.exports}};
  manifest << "],\"counts\":" << counts << "}\n";
  manifest.close();
  if (!manifest) return Fail(ExportError::kOutput);
  if (file.CheckUnchanged()) return Fail(ExportError::kChanged);
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  // Validate all report text before publishing; a non-UTF-8 Linux path must
  // not turn a successfully published tree into a reported serialization error.
  const auto report = nlohmann::json{
      {"output", output.string()},
      {"counts", counts},
      {"peak_rss_bytes",
       static_cast<std::uint64_t>(usage.ru_maxrss) *
           1024}}.dump();
  if (!tree.Publish(output)) return Fail(ExportError::kDestination);
  std::cout << report << '\n';
  return EXIT_SUCCESS;
}
}  // namespace
int main(int argc, char** argv) {
  if (argc != 3 &&
      !(argc == 4 && std::string_view(argv[3]) == "--source")) {
    std::cerr << "usage: abc_structure INPUT.abc OUTPUT_DIR [--source]\n";
    return EXIT_FAILURE;
  }
  constexpr rlim_t kProcessBytes = 1000000000;
  rlimit limit{};
  if (getrlimit(RLIMIT_AS, &limit) != 0) return EXIT_FAILURE;
  limit.rlim_cur = std::min(limit.rlim_cur, kProcessBytes);
  if (setrlimit(RLIMIT_AS, &limit) != 0) return EXIT_FAILURE;
  try {
    return Export(argv[1], argv[2], argc == 4);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
