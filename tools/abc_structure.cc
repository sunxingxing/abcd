#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <type_traits>

#include "abcd/source/source_contract.h"

#include "abcd/corpus/detect.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/record_program.h"
#include "abcd/source/record_source.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/source/source_route.h"
#include "abcd/structure/record_path.h"
#include "abcd/structure/record_writer.h"
#include "process_limit.h"
#include "record_failure_json.h"

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
  kRoles,
};
enum class SourceStatus : unsigned {
  kWritten = 0,
  kModuleContext = 1,
  kNoModule = 2,
  kProgram = 3,
  kIncomplete = 4,
};
enum class SourceMode { kNone, kValueTree };
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
bool FatalRecordSource(const source::RecordSourceResult& result) {
  // Process-level failures only: input I/O, input mutation and allocation
  // failure. A record exceeding its own work/byte/output budget is a typed
  // per-record failure; the remaining records are still exported.
  const auto* failure = std::get_if<source::RecordProgramFailure>(&result);
  return failure && (failure->kind == source::RecordProgramError::kRead ||
                     failure->kind == source::RecordProgramError::kWrite ||
                     failure->kind == source::RecordProgramError::kFileChanged ||
                     failure->kind == source::RecordProgramError::kAllocationFailure);
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
    // Host no-replace rename: a racing pre-existing destination, including a
    // symlink, cannot be overwritten. All children were made in our fresh tree.
#if defined(__APPLE__)
    if (renamex_np(path.c_str(), destination.c_str(), RENAME_EXCL) != 0)
#else
    if (renameat2(AT_FDCWD, path.c_str(), AT_FDCWD, destination.c_str(),
                  RENAME_NOREPLACE) != 0)
#endif
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
           const std::filesystem::path& output, SourceMode mode, source::SourceContract contract,
           const tools::ProcessLimit& limit) {
  // The value-tree route is the only source route; without it only record
  // structure and declarations are exported.
  const bool emit_source = mode == SourceMode::kValueTree;
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
  // Bind each record invocation to this input. The entry reports its actual
  // recovery result; the batch layer does not fabricate route refusals.
  auto identity = source::MakeRouteInputIdentity(*hash, *header);
  identity.contract = contract;
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
  if (emit_source &&
      source::SupportsRecordProgramProfile(header->profile)) {
    auto built =
        analysis::MethodRoleIndex::Build(file, *header, *index, *lookup);
    if (auto* ready = std::get_if<analysis::MethodRoleIndex>(&built))
      roles.emplace(std::move(*ready));
    else
      return Fail(ExportError::kRoles);
  }
  std::optional<analysis::FunctionCreationSites> functions;
  std::optional<analysis::ClassMethodSites> classes;
  if (emit_source && roles) {
    auto indexed_functions = analysis::FunctionCreationSites::Build(file, *header, *index, *lookup);
    auto indexed_classes = analysis::ClassMethodSites::Build(file, *header, *index, *lookup);
    if (!std::holds_alternative<analysis::FunctionCreationSites>(indexed_functions) ||
        !std::holds_alternative<analysis::ClassMethodSites>(indexed_classes)) return Fail(ExportError::kIndex);
    functions = std::get<analysis::FunctionCreationSites>(std::move(indexed_functions));
    classes = std::get<analysis::ClassMethodSites>(std::move(indexed_classes));
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
  // Schema 2: single value-tree route; failures carry an explicit
  // source_failure_domain instead of a result-variant index.
  manifest << "{\"schema\":2,\"view\":\""
           << (emit_source ? "declarations_with_value_source" : "declarations_only")
           << "\",\"sha256\":\"" << Hex(hash->sha256)
           << "\",\"packaging\":" << static_cast<unsigned>(*packaging)
           << ",\"source_contract\":\""
           << source::SourceContractName(identity.contract)
           << "\",\"source_route\":\""
           << (emit_source ? source::SourceRouteName(source::SourceRoute::kValueTree) : "none")
           << "\",\"records\":[";
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
      record_manifest["source_complete"] = false;
      record_manifest["executable"] = false;
      record_manifest["source_route"] =
          std::string(source::SourceRouteName(source::SourceRoute::kValueTree));
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
            const auto built = source::BuildRecordSource(
                file, *header, *index, *lookup, record, *ready, *roles, {}, identity,
                functions ? &*functions : nullptr, classes ? &*classes : nullptr);
            if (FatalRecordSource(built))
              return Fail(ExportError::kRecord, record.offset.value);
            record_manifest["source_complete"] = false;
            record_manifest["executable"] = false;
            if (const auto* artifact = std::get_if<source::PrintedSource>(&built)) {
              std::filesystem::create_directories(
                  (tree.path / source_stem).parent_path());
              std::ofstream program(tree.path / source_path, std::ios::binary);
              program << artifact->text;
              program.close();
              if (!program) return Fail(ExportError::kOutput, record.offset.value);
              const auto positions_path = source_stem.string() + ".entry-positions.json";
              std::ofstream positions(tree.path / positions_path, std::ios::binary);
              // Stream spans directly; do not copy the source or build a second
              // body-sized JSON tree. These are entry-only, not descendant maps.
              positions << "{\"record\":" << record.offset.value
                        << ",\"scope\":\"entry_only\",\"positions\":[";
              bool span_comma = false;
              for (const auto& span : artifact->spans) {
                if (span_comma) positions << ',';
                span_comma = true;
                positions << nlohmann::json{{"pc", span.pc.value},
                    {"origin_kind", source::SpanOriginName(span.origin)},
                    {"begin", {span.begin.byte, span.begin.line, span.begin.column}},
                    {"end", {span.end.byte, span.end.line, span.end.column}}};
              }
              positions << "],\"environment_cleanup\":[";
              bool cleanup_comma = false;
              for (const auto& site : artifact->environment_cleanup) {
                if (cleanup_comma) positions << ',';
                cleanup_comma = true;
                nlohmann::json cleanup{{"pc",site.pc.value},{"range",nullptr}};
                switch (site.kind) {
                  case source::CleanupInstructionKind::kPop: cleanup["kind"] = "pop"; break;
                  case source::CleanupInstructionKind::kRethrow: cleanup["kind"] = "rethrow"; break;
                }
                cleanup["allocation_pc"] = site.allocation ? nlohmann::json(site.allocation->value) : nlohmann::json(nullptr);
                positions << cleanup;
              }
              positions << "]}\n";
              positions.close();
              if (!positions) return Fail(ExportError::kOutput, record.offset.value);
              const bool complete = !artifact->incomplete;
              record_manifest["source_status"] = static_cast<unsigned>(complete ? SourceStatus::kWritten : SourceStatus::kIncomplete);
              record_manifest["source_complete"] = complete;
              record_manifest[complete ? "source" : "incomplete_source"] = source_path;
              record_manifest["entry_positions"] = positions_path;
              const auto methods_path = source_stem.string() + ".method-positions.json";
              std::ofstream method_positions(tree.path / methods_path, std::ios::binary);
              method_positions << "{\"record\":" << record.offset.value
                               << ",\"scope\":\"partial_methods\",\"complete\":false,\"positions\":[";
              bool method_comma = false;
              for (const auto& mapped : artifact->method_spans) {
                if (method_comma) method_positions << ',';
                method_comma = true;
                const auto& span = mapped.span;
                method_positions << nlohmann::json{{"method", mapped.method.value},
                    {"pc", span.pc.value},
                    {"origin_kind", source::SpanOriginName(span.origin)},
                    {"begin", {span.begin.byte, span.begin.line, span.begin.column}},
                    {"end", {span.end.byte, span.end.line, span.end.column}}};
              }
              method_positions << "]}\n";
              method_positions.close();
              if (!method_positions) return Fail(ExportError::kOutput, record.offset.value);
              record_manifest["method_positions"] = methods_path;
              record_manifest["source_contract"] = source::SourceContractName(artifact->contract);
              if (artifact->host_realm)
                record_manifest["host_realm_binding"] = artifact->host_realm->binding;
            } else {
              record_manifest["source_status"] = static_cast<unsigned>(SourceStatus::kProgram);
              tools::DescribeRecordFailure(built, record_manifest);
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
  // Validate all report text before publishing; a non-UTF-8 Linux path must
  // not turn a successfully published tree into a reported serialization error.
  auto report_fields = nlohmann::json{
      {"output", output.string()},
      {"counts", counts}};
  tools::ReportProcessMemory(report_fields, limit);
  const auto report = report_fields.dump();
  if (!tree.Publish(output)) return Fail(ExportError::kDestination);
  std::cout << report << '\n';
  return EXIT_SUCCESS;
}
}  // namespace
int main(int argc, char** argv) {
  SourceMode mode = SourceMode::kNone;
  unsigned modes = 0, language = 0, numeric = 0, constructors = 0;
  bool valid = argc >= 3 && argc <= 6;
  for (int i = 3; valid && i < argc; ++i) {
    const std::string_view option{argv[i]};
    if (option == "--language-arithmetic") { ++language; continue; }
    if (option == "--language-constructors") { ++constructors; continue; }
    if (option == "--language-numeric") { ++numeric; continue; }
    ++modes;
    if (option == "--value-source") mode = SourceMode::kValueTree;
    else valid = false;
  }
  if (!valid || modes > 1 || constructors > 1 || language + numeric > 1 || ((constructors || language || numeric) && mode != SourceMode::kValueTree)) {
    std::cerr << "usage: abc_structure INPUT.abc OUTPUT_DIR [--value-source] [--language-arithmetic|--language-numeric] [--language-constructors]\n"
                 "  --language-arithmetic requires --value-source and selects binary language arithmetic\n"
                 "  --language-numeric requires --value-source and selects unary and binary numeric semantics\n"
                 "  --language-constructors requires --value-source and selects native super semantics\n";
    return EXIT_FAILURE;
  }
  const auto numeric_contract = numeric ? source::SourceContract::kLanguageNumericV1 : language ? source::SourceContract::kLanguageArithmeticV1 : source::kSourceContract;
  const auto contract = constructors ? source::WithLanguageConstructors(numeric_contract) : numeric_contract;
  const auto limit = tools::EstablishProcessLimit();
  if (!tools::ProcessLimitAcceptable(limit)) {
    tools::ReportLimitFailure(std::cerr, limit);
    return EXIT_FAILURE;
  }
  try {
    return Export(argv[1], argv[2], mode, contract, limit);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
