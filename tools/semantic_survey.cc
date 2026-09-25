#include "semantic_survey.h"
#include "record_module.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <exception>
#include <limits>
#include <nlohmann/json.hpp>
#include <ostream>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include "abcd/source/semantic_source.h"
#include "abcd/source/async_completion.h"
#include "abcd/abc/call_type.h"
#include "abcd/source/named_scope.h"
#include "abcd/source/module_exports.h"
#include "abcd/abc/record_attributes.h"
#include "abcd/abc/method_lookup.h"
#include "abcd/analysis/method_roles.h"

namespace {
using namespace abcd;
enum class Stage : std::uint8_t {
  kNoCode,
  kDecode,
  kSemantic,
  kProof,
  kRegion,
  kAst,
  kAstReady,
  kPrint,
  kBodyReady,
  kTemplateReady,
  kNormalize,
  kCount,
};
struct MethodProgress {
  Stage stage;
  std::optional<unsigned> error;
  std::optional<abc::CodeOffset> pc;
  std::optional<abc::Opcode> opcode;
  std::optional<source::PrintedSource> source;
  std::optional<source::PrintedTemplate> source_template;
  bool requires_async = false;
};
constexpr std::size_t kStageCount = static_cast<std::size_t>(Stage::kCount);
constexpr std::size_t kMethodBytes = 64 * 1024 * 1024;
constexpr std::string_view StageLabel(Stage stage) {
  switch (stage) {
    case Stage::kNoCode: return "no_code";
    case Stage::kDecode: return "decode";
    case Stage::kSemantic: return "semantic";
    case Stage::kProof: return "proof";
    case Stage::kRegion: return "region";
    case Stage::kAst: return "ast";
    case Stage::kAstReady: return "ast_ready";
    case Stage::kPrint: return "print";
    case Stage::kBodyReady: return "body_ready";
    case Stage::kTemplateReady: return "template_ready";
    case Stage::kNormalize: return "normalize";
    case Stage::kCount: break;
  }
  std::terminate();
}

MethodProgress Failure(Stage stage, unsigned error, abc::CodeOffset pc,
                       const abc::MethodBody& body) {
  const auto found = std::lower_bound(
      body.instructions.begin(), body.instructions.end(), pc,
      [](const abc::BodyInstruction& instruction, abc::CodeOffset offset) {
        return instruction.offset < offset;
      });
  std::optional<abc::Opcode> opcode;
  if (found != body.instructions.end() && found->offset == pc)
    opcode = found->decoded.opcode;
  return {stage, error, pc, opcode};
}

MethodProgress InspectMethod(const corpus::AbcFile& file,
                             const abc::Header& header,
                             const abc::MetadataIndex& index,
                             const abc::MethodInfo& method,
                             const abc::ModuleInfo* module,
                             const source::ModuleExports* exports,
                             const abc::MethodLookup& methods,
                             const analysis::MethodRoleIndex* roles) {
  if (!method.code) return {Stage::kNoCode, {}, {}, {}};
  const auto& code = *method.code;
  if (code.registers >
      std::numeric_limits<std::uint32_t>::max() - code.arguments)
    return {Stage::kDecode,
            static_cast<unsigned>(abc::BodyError::kRegisterRange), {}, {}};
  const auto decoded = abc::DecodeMethodBody(file, header.profile, code);
  const auto* body = std::get_if<abc::MethodBody>(&decoded);
  if (!body) {
    const auto& failure = std::get<abc::BodyFailure>(decoded);
    return {Stage::kDecode, static_cast<unsigned>(failure.kind),
            failure.offset, {}};
  }
  const auto* original = body;
  std::size_t available = kMethodBytes;
  const auto admit = [&](std::size_t count, std::size_t width) {
    if (count > available / width) return false;
    available -= count * width;
    return true;
  };
  if (!admit(body->instructions.capacity(), sizeof(abc::BodyInstruction)) ||
      !admit(body->tries.capacity(), sizeof(abc::TryRegion)) ||
      !admit(body->handlers.capacity(), sizeof(abc::CatchHandler)))
    return {Stage::kDecode, static_cast<unsigned>(abc::BodyError::kResourceLimit)};
  std::optional<abc::MethodBody> normalized;
  std::vector<abc::CodeOffset> origins;
  std::vector<analysis::AwaitSite> awaits;
  const auto attributes = abc::DecodeMethodAttributes(method.access_flags, header.profile);
  const auto* kind = std::get_if<abc::MethodAttributes>(&attributes);
  if (kind && (kind->kind == abc::FunctionKind::kAsync ||
               kind->kind == abc::FunctionKind::kAsyncArrow)) {
    const auto position = methods.Find(method.offset);
    if (kind->sendable || (kind->kind == abc::FunctionKind::kAsyncArrow &&
        (!roles || !position || roles->At(*position) != analysis::MethodRole::kUnseen)))
      return {Stage::kNormalize, static_cast<unsigned>(source::FunctionEmitError::kKind)};
    const auto call = abc::ReadCallType(file, header, index, method.annotations,
                                      {available});
    const auto* call_type = std::get_if<abc::CallType>(&call);
    if (!call_type)
      return {Stage::kNormalize, static_cast<unsigned>(
          std::get<abc::MetadataError>(call) == abc::MetadataError::kResourceLimit
              ? source::FunctionEmitError::kResourceLimit
              : source::FunctionEmitError::kFrame)};
    const auto layout = abc::BuildFrameLayout(code.registers, code.arguments,
                                             abc::DecodeCallFlags(call_type->value));
    const auto* frame = std::get_if<abc::FrameLayout>(&layout);
    if (!frame)
      return {Stage::kNormalize, static_cast<unsigned>(source::FunctionEmitError::kFrame)};
    auto prepared = source::ProveAsyncCompletion(*body, *frame, available,
        &origins, &awaits, header.profile,
        source::ResourceFailures(source::kSourceContract));
    if (const auto* error = std::get_if<source::FunctionEmitError>(&prepared))
      return {Stage::kNormalize, static_cast<unsigned>(*error)};
    normalized = std::get<abc::MethodBody>(std::move(prepared));
    if (!admit(normalized->instructions.capacity(), sizeof(abc::BodyInstruction)) ||
        !admit(normalized->tries.capacity(), sizeof(abc::TryRegion)) ||
        !admit(normalized->handlers.capacity(), sizeof(abc::CatchHandler)) ||
        !admit(origins.capacity(), sizeof(abc::CodeOffset)) ||
        !admit(awaits.capacity(), sizeof(analysis::AwaitSite)))
      return {Stage::kNormalize, static_cast<unsigned>(source::FunctionEmitError::kResourceLimit)};
    body = &*normalized;
  }
  const auto fail = [&](Stage stage, unsigned error, abc::CodeOffset pc) {
    // Analysis reports normalized PCs; the printer already reports origins.
    if (stage != Stage::kPrint && !origins.empty()) {
      const auto site = std::lower_bound(body->instructions.begin(),
          body->instructions.end(), pc,
          [](const auto& instruction, auto offset) { return instruction.offset < offset; });
      if (site != body->instructions.end() && site->offset == pc)
        pc = origins[static_cast<std::size_t>(site - body->instructions.begin())];
    }
    return Failure(stage, error, pc, *original);
  };
  const auto inspect = [&]() -> MethodProgress {
    auto built = analysis::BuildSemanticMethod(
        *body, header.profile, code.registers + code.arguments, {available}, awaits);
    auto* ir = std::get_if<analysis::SemanticMethod>(&built);
    if (!ir) {
      const auto& failure = std::get<analysis::SemanticFailure>(built);
      return fail(Stage::kSemantic, static_cast<unsigned>(failure.kind), failure.pc);
    }
    for (std::size_t i = 0; i < origins.size(); ++i) ir->instructions[i].origin = origins[i];
    const auto verified = analysis::VerifySemanticSource(
        *body, *ir, code.registers + code.arguments, code.registers,
        {available}, analysis::CapturedValues::kLanguage, {},
        analysis::ClosureWrites::kUnknown, {},
        source::NormalCallResults(source::kSourceContract));
    const auto* proof = std::get_if<analysis::SemanticProof>(&verified);
    if (!proof) {
      const auto& failure = std::get<analysis::SemanticProofFailure>(verified);
      return fail(Stage::kProof, static_cast<unsigned>(failure.kind), failure.pc);
    }
    auto recovered = analysis::BuildSemanticRegions(*ir, available);
    auto* regions = std::get_if<analysis::SemanticRegionTree>(&recovered);
    if (!regions) {
      const auto& failure = std::get<analysis::RegionFailure>(recovered);
      return fail(Stage::kRegion, static_cast<unsigned>(failure.kind), failure.pc);
    }
    source::MethodConstants constants{file, header, index, method.offset,
                                      {available}, module, roles,
                                      source::GlobalScope{}};
    constants.leaf_methods = &methods;
    constants.source_pcs = origins;
    constants.awaits = awaits;
    if (exports) {
      constants.global_scope->module_slots = exports->slot_count();
      // This survey never calls WriteStore: slot identity is known, but no
      // initialization premise may be invented from declaration metadata.
      constants.global_scope->module_exports = exports;
    }
    const auto checked = source::ValidateNamedScopes(
        *body, code.registers + code.arguments, &constants, available,
        analysis::kDefaultSemanticWork);
    const auto* scopes = std::get_if<source::NamedScopes>(&checked);
    if (!scopes) {
      const auto& failure = std::get<source::BodyEmitFailure>(checked);
      return fail(Stage::kAst, static_cast<unsigned>(
          failure.kind == source::BodyEmitError::kResourceLimit ||
                  failure.kind == source::BodyEmitError::kWorkLimit
              ? source::SourceError::kResourceLimit : source::SourceError::kConstant),
          failure.offset);
    }
    const auto lowered = source::BuildSourceAst(
        *ir, *proof, std::move(*regions), available, scopes->admitted);
    if (const auto* failure = std::get_if<source::SourceFailure>(&lowered))
      return fail(Stage::kAst, static_cast<unsigned>(failure->kind), failure->pc);
    const auto& ast = std::get<source::SourceAst>(lowered);
    const auto register_count = code.registers + code.arguments;
    constexpr auto kBindingBytes = sizeof(std::string) + sizeof(std::string_view);
    if (register_count > available / kBindingBytes)
      return {Stage::kPrint,
              static_cast<unsigned>(source::SourceError::kResourceLimit), {}, {}};
    std::vector<std::string> bindings;
    std::vector<std::string_view> names;
    bindings.reserve(register_count);
    names.reserve(register_count);
    for (std::uint32_t i = 0; i < register_count; ++i)
      bindings.push_back("$r" + std::to_string(i));
    for (const auto& name : bindings) names.push_back(name);
    // A parent with no lexical loads can still inherit capture obligations from
    // a child creation. Classify the complete printer artifact, not just the
    // parent's direct read facts; no parent environment is assumed here.
    auto printed = source::PrintSourceTemplate(
        ast, *ir, {"$acc", names}, constants, available, proof);
    if (auto* source = std::get_if<source::PrintedTemplate>(&printed)) {
      if (!source->captures.empty())
        return {Stage::kTemplateReady, {}, {}, {}, {}, std::move(*source)};
      return {Stage::kBodyReady, {}, {}, {}, source::PrintedSource{
          std::move(source->text), std::move(source->spans)}};
    }
    const auto& failure = std::get<source::SourceFailure>(printed);
    return fail(Stage::kPrint, static_cast<unsigned>(failure.kind), failure.pc);
  };
  auto progress = inspect();
  progress.requires_async = normalized.has_value();
  return progress;
}
}  // namespace

int SurveyRecords(const abcd::corpus::AbcFile& file,
                  const abcd::abc::Header& header,
                  const abcd::abc::MetadataIndex& index,
                  const abcd::corpus::FileFingerprint& fingerprint,
                  std::ostream& output) {
  // Keep the complete record denominator, including annotation records and
  // methods without code. Stream results; release all IR between methods.
  // This survey runs the legacy pipeline only (LegacyAllowed); its stage
  // labels predate route receipts and must not be read as value-tree stages.
  output << nlohmann::json{
      {"schema", 6}, {"sha256_bytes", fingerprint.sha256.bytes},
      {"source_contract", source::SourceContractName(source::kSourceContract)},
      {"source_route", source::SourceRouteName(source::SourceRoute::kLegacy)},
      {"abc_version", header.version.bytes},
      {"profile", static_cast<unsigned>(header.profile)},
      {"records", index.records.size()}, {"methods", index.methods.size()},
      {"source_complete", false}} << '\n';
  std::array<std::size_t, kStageCount> counts{};
  const auto packaging = abc::DetectFilePackaging(file, header, index);
  const auto* layout = std::get_if<abc::FilePackaging>(&packaging);
  if (!layout) return EXIT_FAILURE;
  const auto indexed = abc::MethodLookup::Build(index.methods);
  const auto* methods = std::get_if<abc::MethodLookup>(&indexed);
  if (!methods) return EXIT_FAILURE;
  const auto role_index = analysis::MethodRoleIndex::Build(file, header, index, *methods);
  // A failed role proof never licenses arrows; other callable kinds can still
  // be inspected. Each immutable index is built once, not once per creation.
  const auto* roles = std::get_if<analysis::MethodRoleIndex>(&role_index);
  for (const auto& record : index.records) {
    output << nlohmann::json{{"record", record.offset.value},
                            {"descriptor", record.descriptor},
                            {"methods", record.method_count}} << '\n';
    std::optional<RecordModule> bindings;
    if (*layout == abc::FilePackaging::kMerged) {
      auto loaded = ReadRecordModule(file, header, index, record);
      auto* module = std::get_if<std::optional<RecordModule>>(&loaded);
      if (!module) return EXIT_FAILURE;
      bindings = std::move(*module);
    }
    for (std::size_t i = 0; i < record.method_count; ++i) {
      const auto& method = index.methods.at(record.first_method + i);
      const auto progress = InspectMethod(file, header, index, method,
                                          bindings ? &bindings->module : nullptr,
                                          bindings ? &bindings->exports : nullptr,
                                          *methods, roles);
      ++counts.at(static_cast<std::size_t>(progress.stage));
      nlohmann::json row{{"record", record.offset.value},
                         {"method", method.offset.value},
                         {"name", method.name},
                         {"source_route",
                          source::SourceRouteName(source::SourceRoute::kLegacy)},
                         {"stage", static_cast<unsigned>(progress.stage)},
                         {"stage_label", StageLabel(progress.stage)}};
      if (progress.error) row["error"] = *progress.error;
      if (progress.requires_async) row["requires_async_callable"] = true;
      if (progress.pc) row["pc"] = progress.pc->value;
      // Stage-wide failures may carry a default PC. This is context, not a
      // claim that the instruction at that PC caused the failure.
      if (progress.opcode)
        row["opcode_at_pc"] = static_cast<unsigned>(*progress.opcode);
      if (progress.source) {
        if (progress.source->host_realm)
          row["host_realm_binding"] = progress.source->host_realm->binding;
        row["executable"] = false;
        // Detached bodies have only generated '$' private bindings. Embedding
        // must repeat global collision checks against the actual callable scope.
        row["requires_unshadowed_globals"] = true;
        row["requires_module_bindings"] = bindings.has_value();
        row["body"] = progress.source->text;
        row["positions"] = nlohmann::json::array();
        for (const auto& span : progress.source->spans)
          row["positions"].push_back({
              {"pc", span.pc.value},
              {"origin_kind", source::SpanOriginName(span.origin)},
              {"begin", {span.begin.byte, span.begin.line, span.begin.column}},
              {"end", {span.end.byte, span.end.line, span.end.column}}});
      }
      if (progress.source_template) {
        const auto& source = *progress.source_template;
        if (source.host_realm) row["host_realm_binding"] = source.host_realm->binding;
        row["template_text"] = source.text;
        row["executable"] = false;
        row["requires_capture_binding"] = true;
        row["requires_language_captures"] = true;
        row["requires_unshadowed_globals"] = true;
        row["requires_module_bindings"] = bindings.has_value();
        row["captures"] = nlohmann::json::array();
        for (const auto& capture : source.captures)
          row["captures"].push_back({
              {"at", {capture.position.byte, capture.position.line,
                       capture.position.column}},
              {"capture_cursor", capture.position.captures},
              {"required_domain", static_cast<unsigned>(capture.requirement)},
              {"depth", capture.cell.depth.value},
              {"slot", capture.cell.slot.value}});
        row["template_positions"] = nlohmann::json::array();
        for (const auto& span : source.spans)
          row["template_positions"].push_back({
              {"pc", span.pc.value},
              {"origin_kind", source::SpanOriginName(span.origin)},
              {"begin", {span.begin.byte, span.begin.line, span.begin.column}},
              {"end", {span.end.byte, span.end.line, span.end.column}},
              {"begin_capture_cursor", span.begin.captures},
              {"end_capture_cursor", span.end.captures}});
      }
      output << row << '\n';
      if (!output) return EXIT_FAILURE;
    }
  }
  if (file.CheckUnchanged()) return EXIT_FAILURE;
  output << nlohmann::json{{"stage_counts", counts},
                          {"survey_complete", true},
                          {"source_complete", false}} << '\n';
  return output ? EXIT_SUCCESS : EXIT_FAILURE;
}
