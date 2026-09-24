#include "semantic_survey.h"

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
#include "abcd/abc/record_attributes.h"

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
  kCount,
};
struct MethodProgress {
  Stage stage;
  std::optional<unsigned> error;
  std::optional<abc::CodeOffset> pc;
  std::optional<abc::Opcode> opcode;
  std::optional<source::PrintedSource> source;
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
                             const abc::ModuleInfo* module) {
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
  const auto built = analysis::BuildSemanticMethod(
      *body, header.profile, code.registers + code.arguments);
  const auto* ir = std::get_if<analysis::SemanticMethod>(&built);
  if (!ir) {
    const auto& failure = std::get<analysis::SemanticFailure>(built);
    return Failure(Stage::kSemantic, static_cast<unsigned>(failure.kind),
                   failure.pc, *body);
  }
  const auto verified = analysis::VerifySemanticSource(
      *body, *ir, code.registers + code.arguments, code.registers,
      {kMethodBytes});
  const auto* proof = std::get_if<analysis::SemanticProof>(&verified);
  if (!proof) {
    const auto& failure = std::get<analysis::SemanticProofFailure>(verified);
    return Failure(Stage::kProof, static_cast<unsigned>(failure.kind),
                   failure.pc, *body);
  }
  auto recovered = analysis::BuildSemanticRegions(*ir, kMethodBytes);
  auto* regions = std::get_if<analysis::SemanticRegionTree>(&recovered);
  if (!regions) {
    const auto& failure = std::get<analysis::RegionFailure>(recovered);
    return Failure(Stage::kRegion, static_cast<unsigned>(failure.kind),
                   failure.pc, *body);
  }
  const auto lowered = source::BuildSourceAst(
      *ir, *proof, std::move(*regions), kMethodBytes);
  if (const auto* failure = std::get_if<source::SourceFailure>(&lowered))
    return Failure(Stage::kAst, static_cast<unsigned>(failure->kind),
                   failure->pc, *body);
  const auto& ast = std::get<source::SourceAst>(lowered);
  const auto register_count = code.registers + code.arguments;
  constexpr auto kBindingBytes = sizeof(std::string) + sizeof(std::string_view);
  if (register_count > kMethodBytes / kBindingBytes)
    return {Stage::kPrint,
            static_cast<unsigned>(source::SourceError::kResourceLimit), {}, {}};
  std::vector<std::string> bindings;
  std::vector<std::string_view> names;
  bindings.reserve(register_count);
  names.reserve(register_count);
  for (std::uint32_t i = 0; i < register_count; ++i)
    bindings.push_back("$r" + std::to_string(i));
  for (const auto& name : bindings) names.push_back(name);
  source::MethodConstants constants{file, header, index, method.offset,
                                    {kMethodBytes}, module, nullptr,
                                    source::GlobalScope{}};
  auto printed = source::PrintSourceBody(
      ast, *ir, {"$acc", names}, constants, kMethodBytes);
  if (auto* source = std::get_if<source::PrintedSource>(&printed))
    return {Stage::kBodyReady, {}, {}, {}, std::move(*source)};
  const auto& failure = std::get<source::SourceFailure>(printed);
  return Failure(Stage::kPrint, static_cast<unsigned>(failure.kind),
                 failure.pc, *body);
}
}  // namespace

int SurveyRecords(const abcd::corpus::AbcFile& file,
                  const abcd::abc::Header& header,
                  const abcd::abc::MetadataIndex& index,
                  const abcd::corpus::FileFingerprint& fingerprint,
                  std::ostream& output) {
  // Keep the complete record denominator, including annotation records and
  // methods without code. Stream results; release all IR between methods.
  output << nlohmann::json{
      {"schema", 1}, {"sha256_bytes", fingerprint.sha256.bytes},
      {"profile", static_cast<unsigned>(header.profile)},
      {"records", index.records.size()}, {"methods", index.methods.size()},
      {"source_complete", false}} << '\n';
  std::array<std::size_t, kStageCount> counts{};
  const auto packaging = abc::DetectFilePackaging(file, header, index);
  const auto* layout = std::get_if<abc::FilePackaging>(&packaging);
  if (!layout) return EXIT_FAILURE;
  for (const auto& record : index.records) {
    output << nlohmann::json{{"record", record.offset.value},
                            {"descriptor", record.descriptor},
                            {"methods", record.method_count}} << '\n';
    std::optional<abc::ModuleInfo> module;
    if (*layout == abc::FilePackaging::kMerged) {
      const auto parsed = abc::ReadMergedRecordAttributes(file, header, index, record);
      const auto* attributes = std::get_if<abc::RecordAttributes>(&parsed);
      if (!attributes) return EXIT_FAILURE;
      if (attributes->module) {
        auto loaded = abc::ReadModuleInfo(file, header, *attributes->module);
        auto* owner = std::get_if<abc::ModuleInfo>(&loaded);
        if (!owner) return EXIT_FAILURE;
        module = std::move(*owner);
      }
    }
    for (std::size_t i = 0; i < record.method_count; ++i) {
      const auto& method = index.methods.at(record.first_method + i);
      const auto progress = InspectMethod(file, header, index, method,
                                          module ? &*module : nullptr);
      ++counts.at(static_cast<std::size_t>(progress.stage));
      nlohmann::json row{{"record", record.offset.value},
                         {"method", method.offset.value},
                         {"name", method.name},
                         {"stage", static_cast<unsigned>(progress.stage)},
                         {"stage_label", StageLabel(progress.stage)}};
      if (progress.error) row["error"] = *progress.error;
      if (progress.pc) row["pc"] = progress.pc->value;
      // Stage-wide failures may carry a default PC. This is context, not a
      // claim that the instruction at that PC caused the failure.
      if (progress.opcode)
        row["opcode_at_pc"] = static_cast<unsigned>(*progress.opcode);
      if (progress.source) {
        // Detached bodies have only generated '$' private bindings. Embedding
        // must repeat global collision checks against the actual callable scope.
        row["requires_unshadowed_globals"] = true;
        row["body"] = progress.source->text;
        row["positions"] = nlohmann::json::array();
        for (const auto& span : progress.source->spans)
          row["positions"].push_back({
              {"pc", span.pc.value},
              {"begin", {span.begin.byte, span.begin.line, span.begin.column}},
              {"end", {span.end.byte, span.end.line, span.end.column}}});
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
