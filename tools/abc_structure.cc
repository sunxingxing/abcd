#include <fcntl.h>
#include <stdio.h>
#include <sys/resource.h>
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
#include "abcd/source/source_route.h"
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
  kRoles,
  kRouteReceipt
};
enum class SourceStatus : unsigned {
  kWritten = 0,
  kModuleContext = 1,
  kNoModule = 2,
  kProgram = 3,
  kIncomplete = 4,
  // Value-tree route refused the record before any stage ran; no source file
  // exists for it and no legacy writer was consulted (A01).
  kValueRouteRefused = 5
};
enum class SourceMode { kNone, kLegacy, kShared, kValueTree };
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
bool FatalSharedSource(const source::RecordSourceResult& result) {
  return std::visit([](const auto& failure) {
    using T = std::decay_t<decltype(failure)>;
    if constexpr (std::is_same_v<T, source::PrintedSource>) {
      return false;
    } else if constexpr (std::is_same_v<T, source::RouteRefusal>) {
      // Typed route product, not a process-level export failure.
      return false;
    } else if constexpr (std::is_same_v<T, source::RecordProgramFailure>) {
      return failure.kind == source::RecordProgramError::kRead ||
             failure.kind == source::RecordProgramError::kWrite ||
             failure.kind == source::RecordProgramError::kFileChanged ||
             failure.kind == source::RecordProgramError::kResourceLimit ||
             failure.kind == source::RecordProgramError::kOutputLimit;
    } else if constexpr (std::is_same_v<T, analysis::SemanticFailure>) {
      if (failure.kind == analysis::SemanticError::kResourceLimit) return true;
      return std::visit([](const auto& cause) {
        using C = std::decay_t<decltype(cause)>;
        if constexpr (std::is_same_v<C, analysis::RegisterUseFailure>) {
          return cause.kind == analysis::RegisterUseError::kResourceLimit ||
                 cause.kind == analysis::RegisterUseError::kWorkLimit;
        } else if constexpr (requires { C::kResourceLimit; }) {
          if (cause == C::kResourceLimit) return true;
          if constexpr (requires { C::kWorkLimit; }) return cause == C::kWorkLimit;
          return false;
        } else return false;
      }, failure.ssa_cause);
    } else if constexpr (std::is_same_v<T, analysis::SemanticProofFailure>) {
      return failure.kind == analysis::SemanticProofError::kResourceLimit;
    } else if constexpr (std::is_same_v<T, analysis::RegionFailure>) {
      return failure.kind == analysis::RegionError::kResourceLimit;
    } else if constexpr (std::is_same_v<T, source::SourceFailure>) {
      return failure.kind == source::SourceError::kResourceLimit ||
             failure.kind == source::SourceError::kOutput;
    }
  }, result);
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
           const std::filesystem::path& output, SourceMode mode, source::SourceContract contract) {
  const bool emit_source = mode != SourceMode::kNone;
  // Value-tree mode admits the route per record and never plans legacy
  // emission: role indexes and source paths below exist only for the legacy
  // and shared writers, and consulting them would already be a fallback.
  const bool route_value = mode == SourceMode::kValueTree;
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
  const auto route_policy = route_value
                                ? source::RoutePolicy::kRequireValueTree
                                : source::RoutePolicy::kLegacyAllowed;
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
  if (route_value && roles) {
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
  manifest << "{\"schema\":1,\"view\":\""
           << (mode == SourceMode::kShared ? "declarations_with_incomplete_source"
               : route_value ? "declarations_with_value_source"
               : emit_source ? "declarations_with_verified_source"
                           : "declarations_only")
           << "\",\"sha256\":\"" << Hex(hash->sha256)
           << "\",\"packaging\":" << static_cast<unsigned>(*packaging)
           << ",\"source_contract\":\""
           << source::SourceContractName(identity.contract)
           << "\",\"source_route\":\""
           << (route_value ? source::SourceRouteName(source::SourceRoute::kValueTree)
               : emit_source ? source::SourceRouteName(source::SourceRoute::kLegacy)
                           : "none")
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
      if (route_value) {
        record_manifest["source_complete"] = false;
        record_manifest["executable"] = false;
        record_manifest["legacy_writer_used"] = false;
      }
      record_manifest["source_route"] =
          std::string(source::SourceRouteName(route_value ? source::SourceRoute::kValueTree : source::SourceRoute::kLegacy));
      const auto source_stem = std::filesystem::path("sources") /
                               source_paths[record_index].stem;
      const auto source_path = source_stem.string() +
          (mode == SourceMode::kShared ? ".incomplete.mjs" : ".mjs");
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
          } else if (mode == SourceMode::kShared || route_value) {
            const auto built = source::BuildRecordSource(
                file, *header, *index, *lookup, record, *ready, *roles, {},
                nullptr, route_policy, nullptr, nullptr, route_value ? &identity : nullptr,
                functions ? &*functions : nullptr, classes ? &*classes : nullptr);
            if (FatalSharedSource(built))
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
              const bool complete = route_value && !artifact->incomplete;
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
              record_manifest["shared_failure_variant"] = built.index();
              std::visit([&](const auto& failure) {
                using T = std::decay_t<decltype(failure)>;
                if constexpr (std::is_same_v<T, source::RouteRefusal>) {
                  record_manifest["source_failure"] = {
                      {"class", source::RouteFailureClassName(failure.failure.failure_class)},
                      {"reason", source::ValueRouteErrorName(failure.failure.reason)}};
                  if (failure.failure.pc) record_manifest["source_failure"]["pc"] = failure.failure.pc->value;
                } else if constexpr (!std::is_same_v<T, source::PrintedSource>) {
                  record_manifest["source_error"] = static_cast<unsigned>(failure.kind);
                  record_manifest["source_pc"] = failure.pc.value;
                }
              }, built);
            }
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
  // Validate all report text before publishing; a non-UTF-8 Linux path must
  // not turn a successfully published tree into a reported serialization error.
  auto report_fields = nlohmann::json{
      {"output", output.string()},
      {"counts", counts}};
  if (mode != SourceMode::kShared) {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
    // Darwin getrusage reports bytes; Linux reports KiB.
    constexpr std::uint64_t kRssUnitBytes = 1;
#else
    constexpr std::uint64_t kRssUnitBytes = 1024;
#endif
    report_fields["peak_rss_bytes"] =
        static_cast<std::uint64_t>(usage.ru_maxrss) * kRssUnitBytes;
  }
  const auto report = report_fields.dump();
  if (!tree.Publish(output)) return Fail(ExportError::kDestination);
  std::cout << report << '\n';
  return EXIT_SUCCESS;
}
}  // namespace
int main(int argc, char** argv) {
  SourceMode mode = SourceMode::kNone;
  unsigned modes = 0, language = 0, numeric = 0;
  bool valid = argc >= 3 && argc <= 5;
  for (int i = 3; valid && i < argc; ++i) {
    const std::string_view option{argv[i]};
    if (option == "--language-arithmetic") { ++language; continue; }
    if (option == "--language-numeric") { ++numeric; continue; }
    ++modes;
    if (option == "--value-source") mode = SourceMode::kValueTree;
    else if (option == "--shared-source") mode = SourceMode::kShared;
    else if (option == "--source") mode = SourceMode::kLegacy;
    else valid = false;
  }
  if (!valid || modes > 1 || language + numeric > 1 || ((language || numeric) && mode != SourceMode::kValueTree)) {
    std::cerr << "usage: abc_structure INPUT.abc OUTPUT_DIR [--source|--shared-source|--value-source] [--language-arithmetic|--language-numeric]\n"
                 "  --language-arithmetic requires --value-source and selects binary language arithmetic\n"
                 "  --language-numeric requires --value-source and selects unary and binary numeric semantics\n";
    return EXIT_FAILURE;
  }
  const auto contract = numeric ? source::SourceContract::kLanguageNumericV1 : language ? source::SourceContract::kLanguageArithmeticV1 : source::kSourceContract;
#if !defined(__APPLE__)
  // Darwin rejects this Linux virtual-address-space limit; it also cannot
  // establish the process RSS contract. Existing bounded stage allocations
  // remain active on both hosts. Do not claim a measured 1 GB RSS guarantee.
  constexpr rlim_t kProcessBytes = 1000000000;
  rlimit limit{};
  if (getrlimit(RLIMIT_AS, &limit) != 0) return EXIT_FAILURE;
  limit.rlim_cur = std::min(limit.rlim_cur, kProcessBytes);
  if (setrlimit(RLIMIT_AS, &limit) != 0) return EXIT_FAILURE;
#endif
  try {
    return Export(argv[1], argv[2], mode, contract);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
