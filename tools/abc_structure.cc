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
  kRead,
};
enum class SourceStatus : unsigned {
  kWritten = 0,
  kModuleContext = 1,
  kNoModule = 2,
  kProgram = 3,
  kIncomplete = 4,
};
enum class SourceMode { kNone, kValueTree };
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
  // Process-level faults are observed out of band, never inferred from how a
  // stage folded its typed error (S3): a sticky real allocation failure, a
  // sticky input read failure, or an input change. They are rechecked around
  // every record and whole-file index, including early-ending branches.
  const auto process_failed = [&](std::uint32_t entity) -> std::optional<int> {
    if (tools::AllocationFailureObserved()) {
      std::cerr << nlohmann::json{{"allocation_failure", true}, {"entity", entity}} << '\n';
      return Fail(ExportError::kRecord, entity);
    }
    if (file.ReadFailed()) return Fail(ExportError::kRead, entity);
    if (file.CheckUnchanged()) return Fail(ExportError::kChanged, entity);
    return std::nullopt;
  };
  // Whole-file source indexes. Their failure is reported per record with
  // typed causes (kFileIndex) unless a process-level fault was observed.
  std::optional<source::RecordProgramFailure> file_failure;
  if (emit_source && !source::SupportsRecordProgramProfile(header->profile))
    file_failure = source::RecordProgramFailure{source::RecordProgramError::kProfile, {}};
  const auto index_failure = [&](const auto& error) -> std::optional<int> {
    if (const auto code = process_failed(0)) return code;
    source::RecordProgramFailure failure{source::RecordProgramError::kFileIndex, {}};
    std::visit([&](const auto& cause) {
      using T = std::decay_t<decltype(cause)>;
      if constexpr (std::is_enum_v<T>) source::AppendFailureCause(failure.causes, cause);
      else if constexpr (requires { cause.kind; }) source::AppendFailureCause(failure.causes, cause.kind);
    }, error);
    file_failure = failure;
    return std::nullopt;
  };
  std::optional<analysis::MethodRoleIndex> roles;
  std::optional<analysis::FunctionCreationSites> functions;
  std::optional<analysis::ClassMethodSites> classes;
  if (emit_source && !file_failure) {
    auto built = analysis::MethodRoleIndex::Build(file, *header, *index, *lookup);
    if (auto* ready = std::get_if<analysis::MethodRoleIndex>(&built)) {
      roles.emplace(std::move(*ready));
    } else if (const auto code = index_failure(std::get<analysis::MethodRoleFailure>(built).error)) {
      return *code;
    }
  }
  if (emit_source && !file_failure) {
    auto indexed_functions = analysis::FunctionCreationSites::Build(file, *header, *index, *lookup);
    if (auto* ready = std::get_if<analysis::FunctionCreationSites>(&indexed_functions)) {
      functions = std::move(*ready);
    } else if (const auto code = index_failure(std::get<analysis::FunctionSiteFailure>(indexed_functions).error)) {
      return *code;
    }
  }
  if (emit_source && !file_failure) {
    auto indexed_classes = analysis::ClassMethodSites::Build(file, *header, *index, *lookup);
    if (auto* ready = std::get_if<analysis::ClassMethodSites>(&indexed_classes)) {
      classes = std::move(*ready);
    } else if (const auto code = index_failure(std::get<analysis::ClassSiteFailure>(indexed_classes).error)) {
      return *code;
    }
  }
  // Source paths are planned per record: a record with unusable identity
  // evidence, an encoding limit or a case-folded collision gets its own typed
  // failure; only the planning budget is file-wide (S3).
  std::vector<structure::RecordSourcePath> source_paths;
  if (emit_source) {
    auto planned = structure::PlanRecordSourcePathSet(file, *header, *index);
    if (const auto code = process_failed(0)) return *code;
    if (const auto* error = std::get_if<structure::PathError>(&planned)) {
      if (!file_failure) {
        file_failure = source::RecordProgramFailure{source::RecordProgramError::kFileIndex, {}};
        source::AppendFailureCause(file_failure->causes, *error);
      }
    } else {
      source_paths = std::get<std::vector<structure::RecordSourcePath>>(std::move(planned));
    }
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
           << "\",\"abc_version\":"
           << nlohmann::json(std::vector<unsigned>(header->version.bytes.begin(),
                                                   header->version.bytes.end()))
           << ",\"records\":[";
  structure::RecordCounts totals;
  bool comma = false;
  std::size_t record_index = 0;
  for (const auto& record : index->records) {
    if (const auto code = process_failed(record.offset.value)) return *code;
    const auto planned = structure::PlanRecordPath(record);
    const auto* path = std::get_if<structure::RecordPath>(&planned);
    if (!path) {
      // No per-record files can be named: list the record with its typed
      // path failure and continue with the others (S3).
      nlohmann::json failed{{"entity", record.offset.value},
          {"structure_failure", {{"domain", source::FailureDomain<structure::PathError>()},
                                 {"value", static_cast<unsigned>(std::get<structure::PathError>(planned))}}}};
      if (comma) manifest << ',';
      comma = true;
      manifest << failed;
      ++record_index;
      continue;
    }
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
      // Process-level faults and output write failures stop the export; any
      // other structural fault (including budgets) is this record's (S3).
      if (const auto code = process_failed(record.offset.value)) return *code;
      bool output_fault = false;
      nlohmann::json typed;
      std::visit([&](const auto& error) {
        using T = std::decay_t<decltype(error)>;
        if constexpr (std::is_enum_v<T>) {
          typed = {{"domain", source::FailureDomain<T>()}, {"value", static_cast<unsigned>(error)}};
        } else {
          typed = {{"domain", source::FailureDomain<std::decay_t<decltype(error.kind)>>()},
                   {"value", static_cast<unsigned>(error.kind)}};
        }
        if constexpr (std::is_same_v<T, structure::StructureFault>)
          output_fault = error == structure::StructureFault::kWrite;
        if constexpr (std::is_same_v<T, source::EmitError>)
          output_fault = error == source::EmitError::kWrite;
      }, failure.error);
      metadata.close();
      declarations.close();
      // A write that only failed while flushing is an output fault too.
      output_fault |= !metadata || !declarations;
      // The partial record is withdrawn; a failed removal would publish it.
      std::error_code removed_metadata, removed_declarations;
      std::filesystem::remove(tree.path / metadata_path, removed_metadata);
      std::filesystem::remove(tree.path / declaration_path, removed_declarations);
      if (output_fault || removed_metadata || removed_declarations) {
        std::cerr << nlohmann::json{{"structure_failure", typed}, {"entity", failure.entity.value}} << '\n';
        return Fail(ExportError::kOutput, record.offset.value);
      }
      typed["entity"] = failure.entity.value;
      nlohmann::json failed{{"entity", record.offset.value}, {"structure_failure", typed}};
      if (comma) manifest << ',';
      comma = true;
      manifest << failed;
      ++record_index;
      continue;
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
      const auto* source_record_path =
          source_paths.empty() ? nullptr : std::get_if<structure::RecordPath>(&source_paths[record_index]);
      if (!source_record_path) {
        // Whole-file planning failed (file_failure is set), or this record
        // has its own typed path failure.
        auto failure = file_failure.value_or(
            source::RecordProgramFailure{source::RecordProgramError::kSourcePath, {}});
        if (!file_failure)
          source::AppendFailureCause(failure.causes,
                                     std::get<structure::PathError>(source_paths[record_index]));
        record_manifest["source_status"] = static_cast<unsigned>(SourceStatus::kProgram);
        tools::DescribeRecordFailure(failure, record_manifest);
        if (comma) manifest << ',';
        comma = true;
        manifest << record_manifest;
        ++record_index;
        if (const auto code = process_failed(record.offset.value)) return *code;
        continue;
      }
      const auto source_stem = std::filesystem::path("sources") / source_record_path->stem;
      auto source_path = source_stem.string() + ".mjs";
      record_manifest["planned_source"] = source_path;
      record_manifest["source_path_origin"] =
          static_cast<unsigned>(source_record_path->origin);
      if (file_failure) {
        record_manifest["source_status"] =
            static_cast<unsigned>(SourceStatus::kProgram);
        tools::DescribeRecordFailure(*file_failure, record_manifest);
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
            const bool arkts = source::LazyImports(identity.contract) == source::LazyImportSemantics::kArkTsLazyImport && std::find(module->request_phases.begin(), module->request_phases.end(),
                abc::ModuleRequestPhase::kLazy) != module->request_phases.end();
            if (arkts) {
              source_path = source_stem.string() + ".ets";
              record_manifest["planned_source"] = source_path;
              record_manifest["source_language"] = "arkts";
            }
            const auto built = source::BuildRecordSource(
                file, *header, *index, *lookup, record, *ready, *roles, {}, identity,
                functions ? &*functions : nullptr, classes ? &*classes : nullptr);
            // Out-of-band process faults stop the export; folded error codes
            // and budgets do not.
            const bool process_fault = tools::AllocationFailureObserved() ||
                                       file.ReadFailed() || file.CheckUnchanged().has_value();
            if (tools::RecordFailureIsFatal(built, process_fault)) {
              // Typed diagnostic for the fatal record failure (S2).
              nlohmann::json failure;
              tools::DescribeRecordFailure(built, failure);
              std::cerr << nlohmann::json{{"record_failure", failure},
                                          {"entity", record.offset.value}} << '\n';
              return Fail(ExportError::kRecord, record.offset.value);
            }
            record_manifest["source_complete"] = false;
            record_manifest["executable"] = false;
            if (const auto* artifact = std::get_if<source::PrintedSource>(&built)) {
              if(artifact->arkts) {
                source_path=source_stem.string()+".ets";
                record_manifest["planned_source"]=source_path;
                record_manifest["source_language"]="arkts";
              }
              tools::DescribeRecordFailure(built, record_manifest);
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
          // Module context failures are this record's; process faults are
          // rechecked out of band below.
          record_manifest["source_status"] =
              static_cast<unsigned>(SourceStatus::kModuleContext);
          tools::DescribeModuleContextFailure(failure, record_manifest);
        }
      }
    }
    if (const auto code = process_failed(record.offset.value)) return *code;
    if (comma) manifest << ',';
    comma = true;
    manifest << record_manifest;
    if (!manifest) return Fail(ExportError::kOutput);
    ++record_index;
  }
  if (const auto code = process_failed(0)) return *code;
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
  unsigned modes = 0, language = 0, numeric = 0, constructors = 0, source = 0;
  bool valid = argc >= 3 && argc <= 6;
  for (int i = 3; valid && i < argc; ++i) {
    const std::string_view option{argv[i]};
    if (option == "--language-arithmetic") { ++language; continue; }
    if (option == "--language-constructors") { ++constructors; continue; }
    if (option == "--language-numeric") { ++numeric; continue; }
    if (option == "--language-source") { ++source; continue; }
    ++modes;
    if (option == "--value-source") mode = SourceMode::kValueTree;
    else valid = false;
  }
  if (!valid || modes > 1 || constructors > 1 || language + numeric > 1 || source > 1 ||
      (source && (constructors || language || numeric)) ||
      ((constructors || language || numeric || source) && mode != SourceMode::kValueTree)) {
    std::cerr << "usage: abc_structure INPUT.abc OUTPUT_DIR [--value-source] [--language-arithmetic|--language-numeric] [--language-constructors] | [--language-source]\n"
                 "  --language-arithmetic requires --value-source and selects binary language arithmetic\n"
                 "  --language-numeric requires --value-source and selects unary and binary numeric semantics\n"
                 "  --language-constructors requires --value-source and selects native super semantics\n"
                 "  --language-source requires --value-source and selects numeric and constructor semantics plus\n"
                 "    the source premises approved on 2026-10-13 (docs/design/source-recovery/language-source.md)\n";
    return EXIT_FAILURE;
  }
  const auto numeric_contract = numeric ? source::SourceContract::kLanguageNumericV1 : language ? source::SourceContract::kLanguageArithmeticV1 : source::kSourceContract;
  const auto contract = source ? source::SourceContract::kLanguageSourceV4
      : constructors ? source::WithLanguageConstructors(numeric_contract) : numeric_contract;
  tools::InstallAllocationFailureMonitor();
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
