#include "../src/source/non_executable_json.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>

#include "abcd/abc/metadata_index.h"
#include "abcd/abc/call_type.h"
#include "abcd/abc/frame_layout.h"
#include "abcd/source/value_stage.h"
#include "abcd/abc/method_body.h"
#include "abcd/abc/method_lookup.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/analysis/await_region.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/exception_edge.h"
#include "abcd/analysis/semantic_validation.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/printed_source.h"
#include "abcd/source/printed_source.h"
#include "abcd/source/method_context.h"
#include "abcd/source/record_program.h"
#include "abcd/source/source_route.h"
#include "abcd/abc/pool_reference.h"
#include "process_limit.h"
#include "value_source_inspect.h"

namespace {
using namespace abcd;
nlohmann::json Slot(const analysis::ScalarSlot& slot) {
  if (const auto* reg = std::get_if<abc::RegisterId>(&slot))
    return nlohmann::json{{"register", reg->value}};
  return nlohmann::json{{"accumulator", true}};
}
void DescribeCoroutine(nlohmann::json& output,
                       const abc::DecodedInstruction& instruction,
                       std::uint64_t frame_size) {
  const auto lifted = analysis::LiftCoroutineInstruction(instruction, frame_size);
  if (const auto* transfer =
          std::get_if<analysis::CoroutineTransfer>(&lifted)) {
    output["coroutine_transfer"] = {
        {"kind", static_cast<unsigned>(transfer->kind)},
        {"state", Slot(transfer->state)}, {"value", Slot(transfer->value)}};
  } else if (const auto* read = std::get_if<analysis::CoroutineRead>(&lifted)) {
    output["coroutine_read"] = {{"kind", static_cast<unsigned>(read->kind)},
                                {"state", Slot(read->state)}};
  } else if (const auto error = std::get<analysis::CoroutineError>(lifted);
             error != analysis::CoroutineError::kUnsupportedOpcode) {
    output["coroutine_error"] = static_cast<unsigned>(error);
  }
}
enum class InspectError : std::uint8_t {
  kArguments,
  kOpen,
  kHash,
  kHeader,
  kIndex,
  kMethod,
  kBody,
  kSemantic,
  kProof,
  kRegion,
  kAst,
  kFrame,
  kPrint,
  // Appended: the result could not be written intact to stdout.
  kOutput,
};
enum class InspectDetail : std::uint8_t { kSummary, kInstructions };
// Set once the process limit is established; argument errors precede it.
const abcd::tools::ProcessLimit* g_process_limit = nullptr;
void ReportFailure(nlohmann::json failure) {
  if (g_process_limit) abcd::tools::ReportProcessMemory(failure, *g_process_limit);
  std::cerr << failure << '\n';
}
int Fail(InspectError kind, std::uint32_t pc = 0) {
  ReportFailure({{"error", static_cast<unsigned>(kind)}, {"pc", pc}});
  return EXIT_FAILURE;
}
int FailStage(InspectError kind, unsigned stage_error,
              std::uint32_t pc) {
  ReportFailure({{"error", static_cast<unsigned>(kind)},
                 {"stage_error", stage_error}, {"pc", pc}});
  return EXIT_FAILURE;
}
// Single-object views: the JSON object a view writes to stdout gains the
// process report (tools/process_limit.h). The view's own output channel and
// exit code are kept. Output is buffered only to merge the fields; a capture
// write failure or a non-JSON result is an output failure, never a silent
// partial publication. Streaming views (--instructions) are not captured.
template <class Run>
int RunWithProcessReport(const abcd::tools::ProcessLimit& limit, Run&& run) {
  std::ostringstream captured;
  auto* const previous = std::cout.rdbuf(captured.rdbuf());
  int code = EXIT_FAILURE;
  try {
    code = run();
  } catch (...) {
    std::cout.rdbuf(previous);
    throw;
  }
  // rdbuf() clears the stream state: read the capture state first.
  const bool captured_ok = static_cast<bool>(std::cout);
  std::cout.rdbuf(previous);
  if (!captured_ok) return Fail(InspectError::kOutput);
  auto text = std::move(captured).str();
  std::size_t end = text.size();
  while (end > 0 && text[end - 1] == '\n') --end;
  if (end == 0) return code;  // The view reported only to stderr.
  const auto begin = text.rfind('\n', end - 1);
  const auto start = begin == std::string::npos ? 0 : begin + 1;
  auto last = nlohmann::json::parse(text.begin() + static_cast<std::ptrdiff_t>(start),
                                    text.begin() + static_cast<std::ptrdiff_t>(end), nullptr, false);
  if (last.is_discarded() || !last.is_object()) return Fail(InspectError::kOutput);
  abcd::tools::ReportProcessMemory(last, limit);
  const auto merged = last.dump();
  text.replace(start, end - start, merged);
  // Re-sample once the merged copy exists so its allocation is counted.
  abcd::tools::ReportProcessMemory(last, limit);
  text.replace(start, merged.size(), last.dump());
  std::cout << text;
  return std::cout ? code : EXIT_FAILURE;
}
// Streaming views keep their per-line output; the process report follows as
// one final JSON line after a successful stream.
int StreamWithProcessReport(const abcd::tools::ProcessLimit& limit, int code) {
  if (code != EXIT_SUCCESS || !std::cout) return code == EXIT_SUCCESS ? EXIT_FAILURE : code;
  nlohmann::json report = nlohmann::json::object();
  abcd::tools::ReportProcessMemory(report, limit);
  std::cout << report << '\n';
  return std::cout ? EXIT_SUCCESS : EXIT_FAILURE;
}
int Inspect(const std::filesystem::path& path, std::uint32_t entity,
            std::optional<source::SourceForm> form, InspectDetail detail,
            ValueSourceScope scope = ValueSourceScope::kMember,
            source::SourceContract contract = source::kSourceContract) {
  auto opened = corpus::AbcFile::Open(path);
  const auto* owned = std::get_if<std::unique_ptr<corpus::AbcFile>>(&opened);
  if (!owned) return Fail(InspectError::kOpen);
  const auto& file = **owned;
  const auto fingerprint = file.Fingerprint();
  const auto* hash = std::get_if<corpus::FileFingerprint>(&fingerprint);
  if (!hash) return Fail(InspectError::kHash);
  const auto parsed = abc::ParseHeader(
      std::span(hash->prefix).first(hash->prefix_size), hash->size);
  const auto* header = std::get_if<abc::Header>(&parsed);
  if (!header || header->checksum != hash->adler32)
    return Fail(InspectError::kHeader);
  auto identity = source::MakeRouteInputIdentity(*hash, *header);
  identity.contract = contract;
  constexpr std::size_t kIndexBytes = 256 * 1024 * 1024;
  const auto indexed = abc::BuildMetadataIndex(file, *header, {kIndexBytes});
  const auto* index = std::get_if<abc::MetadataIndex>(&indexed);
  if (!index) return Fail(InspectError::kIndex);
  const auto method = std::find_if(
      index->methods.begin(), index->methods.end(),
      [entity](const abc::MethodInfo& item) {
        return item.offset.value == entity;
      });
  if (method == index->methods.end() || !method->code)
    return Fail(InspectError::kMethod);
  if (scope == ValueSourceScope::kRecordPrintBudget || scope == ValueSourceScope::kConditionalValues || scope == ValueSourceScope::kConditionalFamily ||
      scope == ValueSourceScope::kFamilyDraft)
    return InspectValueSource(file, *header, *index, *method,
        identity, std::cout, scope);
  const auto& code = *method->code;
  if (code.registers >
      std::numeric_limits<std::uint32_t>::max() - code.arguments)
    return Fail(InspectError::kBody);
  const auto decoded = abc::DecodeMethodBody(file, header->profile, code);
  const auto* body = std::get_if<abc::MethodBody>(&decoded);
  if (!body) {
    const auto& failure = std::get<abc::BodyFailure>(decoded);
    return Fail(InspectError::kBody, failure.offset.value);
  }
  if (form) {
    if (*form == source::SourceForm::kRecord)
      return InspectValueSource(file, *header, *index, *method,
          identity, std::cout, ValueSourceScope::kRecordTree);
    return InspectValueSource(file, *header, *index, *method,
        identity, std::cout);
  }
  if (detail == InspectDetail::kInstructions) {
    constexpr std::size_t kDispatchBytes = 16 * 1024 * 1024;
    const auto dispatched = analysis::BuildExceptionDispatch(
        *body, header->profile, {kDispatchBytes});
    const auto* dispatch = std::get_if<analysis::ExceptionDispatch>(&dispatched);
    constexpr std::size_t kAwaitRegionBytes = 1024 * 1024;
    const auto awaited = analysis::FindAwaitRegions(
        *body, header->profile, code.registers + code.arguments,
        kAwaitRegionBytes);
    const auto* regions = std::get_if<std::vector<analysis::AwaitRegion>>(&awaited);
    std::size_t next_region = 0;
    for (const auto& site : body->instructions) {
      nlohmann::json operands = nlohmann::json::array();
      for (std::size_t i = 0; i < site.decoded.operand_count; ++i) {
        const auto& operand = site.decoded.operands[i];
        nlohmann::json value{{"kind", static_cast<unsigned>(operand.kind)},
                             {"bits", operand.raw_bits}};
        if (operand.kind == abc::OperandKind::kMethod) {
          const auto reference = abc::ResolvePoolReference(
              file, *header, *index, method->offset,
              {static_cast<std::uint32_t>(operand.raw_bits)},
              abc::PoolEntityKind::kMethod);
          if (const auto* pool = std::get_if<abc::PoolReference>(&reference))
            value["method_entity"] = pool->offset.value;
          else
            value["reference_error"] =
                static_cast<unsigned>(std::get<abc::MetadataError>(reference));
        }
        if (operand.kind == abc::OperandKind::kLiteralArray) {
          const auto reference = abc::ResolvePoolReference(
              file, *header, *index, method->offset,
              {static_cast<std::uint32_t>(operand.raw_bits)},
              abc::PoolEntityKind::kLiteralArray);
          if (const auto* pool = std::get_if<abc::PoolReference>(&reference)) {
            const auto literal = abc::ReadLiteralArray(file, *header, pool->offset);
            if (const auto* buffer = std::get_if<abc::LiteralArray>(&literal)) {
              value["literal"] = nlohmann::json::array();
              for (const auto& entry : buffer->entries) {
                nlohmann::json item{{"tag", static_cast<unsigned>(entry.tag)}};
                if (const auto* bits = std::get_if<abc::LiteralBits>(&entry.value))
                  item["bits"] = bits->value;
                else if (entry.tag == abc::LiteralTag::kString) {
                  const auto text = abc::ReadStringEntity(
                      file, *header, std::get<abc::EntityOffset>(entry.value));
                  if (const auto* units = std::get_if<std::u16string>(&text))
                    item["utf16"] = std::vector<std::uint16_t>(units->begin(), units->end());
                }
                value["literal"].push_back(std::move(item));
              }
            }
          }
        }
        operands.push_back(std::move(value));
      }
      nlohmann::json output{{"pc", site.offset.value},
          {"opcode", static_cast<unsigned>(site.decoded.opcode)},
          {"operands", std::move(operands)}};
      DescribeCoroutine(output, site.decoded, code.registers + code.arguments);
      const auto semantic = analysis::LiftSemanticInstruction(
          site, header->profile, code.registers + code.arguments);
      if (const auto* lifted = std::get_if<analysis::SemanticInstruction>(&semantic)) {
        output["normal_effects"] = static_cast<unsigned>(lifted->effects.normal);
        output["before_throw_effects"] =
            static_cast<unsigned>(lifted->effects.before_throw);
        output["exceptional_completion"] =
            static_cast<unsigned>(lifted->effects.exceptional);
        if (const auto* created =
                std::get_if<analysis::ClassCreation>(&lifted->operation)) {
          output["class_creation"] = {
              {"constructor_pool", created->constructor.value},
              {"literal_pool", created->literal.value},
              {"length", created->length},
              {"parent_register", created->parent.value},
              {"cache_slot", created->cache_slot}};
        }
        if (const auto* accessor =
                std::get_if<analysis::DefineAccessor>(&lifted->operation)) {
          output["accessor_definition"] = {
              {"object_register", accessor->object.value},
              {"key_register", accessor->key.value},
              {"getter_register", accessor->getter.value},
              {"setter_register", accessor->setter.value},
              {"name_flag", Slot(analysis::Accumulator{})}};
        }
        if (const auto* write =
                std::get_if<analysis::IndexedWrite>(&lifted->operation)) {
          output["indexed_definition"] = {
              {"object_register", write->object.value},
              {"index", write->index}, {"value", Slot(analysis::Accumulator{})}};
          if (write->cache_slot)
            output["indexed_definition"]["cache_slot"] = *write->cache_slot;
        }
        if (dispatch) {
          if (const auto edge = analysis::ResolveExceptionEdge(*lifted, *dispatch)) {
            nlohmann::json exception{{"register_state_before_pc", edge->before.value}};
            if (edge->target) {
              exception["try"] = edge->target->region.value;
              exception["handler"] = edge->target->handler.value;
              exception["handler_pc"] = edge->target->handler_pc.value;
            } else {
              exception["propagates_to_caller"] = true;
            }
            if (const auto* thrown = std::get_if<analysis::ThrownValue>(&edge->exception)) {
              // Raw throw payloads are VM slots. Refuse unhandled payloads at
              // this boundary instead of silently omitting their identity.
              exception["explicit_throw"] = true;
              if (std::holds_alternative<analysis::Accumulator>(thrown->value))
                exception["value"] = Slot(analysis::Accumulator{});
              else if (const auto* reg = std::get_if<abc::RegisterId>(&thrown->value))
                exception["value"] = Slot(*reg);
              else
                return FailStage(InspectError::kSemantic,
                    static_cast<unsigned>(analysis::SemanticError::kMalformedInstruction),
                    site.offset.value);
            } else {
              exception["pending_exception"] = true;
            }
            output["exception_edge"] = std::move(exception);
          }
        }
      } else {
        const auto& failure = std::get<analysis::SemanticFailure>(semantic);
        output["semantic_error"] = static_cast<unsigned>(failure.kind);
        if (failure.non_executable) output["non_executable"] = source::NonExecutableJson(*failure.non_executable);
      }
      if (!dispatch && site.offset == body->instructions.front().offset) {
        const auto& failure = std::get<analysis::DispatchFailure>(dispatched);
        output["exception_dispatch_failure"] = {
            {"kind", static_cast<unsigned>(failure.kind)},
            {"pc", failure.offset.value}};
      }
      if (regions && next_region < regions->size() &&
          (*regions)[next_region].await == site.offset) {
        const auto& region = (*regions)[next_region++];
        output["await_region"] = {
            {"suspend_pc", region.suspend.value},
            {"resume_pc", region.resume.value}, {"mode_pc", region.mode.value},
            {"branch_pc", region.branch.value}, {"reject_pc", region.reject.value},
            {"continuation_pc", region.continuation.value},
            {"state_register", region.state.value},
            {"result_register", region.result.value},
            {"mode_register", region.resume_mode.value}};
      } else if (!regions && site.offset == body->instructions.front().offset) {
        const auto& failure = std::get<analysis::AwaitRegionFailure>(awaited);
        output["await_region_failure"] = {
            {"kind", static_cast<unsigned>(failure.kind)}};
        if (failure.pc) output["await_region_failure"]["pc"] = failure.pc->value;
      }
      std::cout << output << '\n';
    }
    return std::cout ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  const auto built = analysis::BuildSemanticMethod(
      *body, header->profile, code.registers + code.arguments);
  const auto* method_ir = std::get_if<analysis::SemanticMethod>(&built);
  if (!method_ir) {
    const auto& failure = std::get<analysis::SemanticFailure>(built);
    nlohmann::json report{{"error", static_cast<unsigned>(InspectError::kSemantic)},
                         {"semantic_error", static_cast<unsigned>(failure.kind)},
                         {"pc", failure.pc.value}};
    if (failure.non_executable) report["non_executable"] = source::NonExecutableJson(*failure.non_executable);
    ReportFailure(report);
    return EXIT_FAILURE;
  }
  std::cout << nlohmann::json{
                   {"entity", entity},
                   {"instructions", method_ir->instructions.size()},
                   {"blocks", method_ir->flow.blocks.size()},
                   {"ssa_values", method_ir->ssa.Values().size()}}
            << '\n';
  return std::cout ? EXIT_SUCCESS : EXIT_FAILURE;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3 || argc > 7) {
    std::cerr << "usage: abc_semantic INPUT.abc METHOD_ENTITY [--instructions]\n"
                 "       abc_semantic INPUT.abc METHOD_ENTITY --function-source|--record-source|--record-print-budget\n"
                 "                    [--language-arithmetic|--language-numeric] [--language-constructors] | [--language-source]\n"
                                  "       abc_semantic INPUT.abc METHOD_ENTITY --conditional-values\n"
                 "       abc_semantic INPUT.abc METHOD_ENTITY --conditional-family\n"
                 "       abc_semantic INPUT.abc METHOD_ENTITY --family-source-draft\n"
                 "  every source form uses the unified value-tree route; --value-source is\n"
                 "  accepted for compatibility and changes nothing\n"
                 "  --family-source-draft retains supported body prefixes with marked gaps, not executable source\n"
                 "  --conditional-values inspects open capture obligations; it emits no source\n"
                 "  --conditional-family follows actual child creations; it emits no source\n"
                 "  exactly one source form or view per invocation; duplicates and conflicting\n"
                 "  selectors are rejected regardless of option order\n"
                 "  unsupported stages return a typed route receipt with no source text\n";
    return Fail(InspectError::kArguments);
  }
  std::uint32_t entity = 0;
  const std::string_view token{argv[2]};
  const auto number = std::from_chars(
      token.data(), token.data() + token.size(), entity);
  if (number.ec != std::errc{} ||
      number.ptr != token.data() + token.size())
    return Fail(InspectError::kArguments);
  // Parse every option first, then validate combinations independent of
  // order before any file access.
  std::optional<source::SourceForm> form;
  unsigned forms = 0, instructions = 0, value_routes = 0;
  unsigned language_arithmetic = 0, language_numeric = 0, language_constructors = 0, language_source = 0;
  unsigned conditional_values = 0, conditional_families = 0, family_drafts = 0, print_budget = 0;
  for (int i = 3; i < argc; ++i) {
    const std::string_view option{argv[i]};
    if (option == "--language-arithmetic") ++language_arithmetic;
    else if (option == "--language-constructors") ++language_constructors;
    else if (option == "--language-numeric") ++language_numeric;
    else if (option == "--language-source") ++language_source;
    else if (option == "--family-source-draft") ++family_drafts;
    else if (option == "--conditional-family") ++conditional_families;
    else if (option == "--conditional-values") ++conditional_values;
    else if (option == "--value-source") ++value_routes;
    else if (option == "--instructions") ++instructions;
    else if (option == "--function-source") { ++forms; form = source::SourceForm::kFunction; }
    else if (option == "--record-print-budget") { ++forms; ++print_budget; form=source::SourceForm::kRecord; }
    else if (option == "--record-source") { ++forms; form = source::SourceForm::kRecord; }
    else return Fail(InspectError::kArguments);
  }
  const unsigned views = conditional_values + conditional_families + family_drafts;
  const bool contract_options = language_constructors || language_arithmetic || language_numeric || language_source;
  if (forms > 1 || instructions > 1 || value_routes > 1 || views > 1 ||
      language_constructors > 1 || language_arithmetic + language_numeric > 1 || language_source > 1 ||
      (language_source && (language_constructors || language_arithmetic || language_numeric)) ||
      forms + instructions + views > 1 || (contract_options && !forms && !value_routes) ||
      (value_routes && (instructions || views)))
    return Fail(InspectError::kArguments);
  // Same process boundary as every CLI (tools/process_limit.h); established
  // after argument validation and before any file access.
  const auto limit = abcd::tools::EstablishProcessLimit();
  if (!abcd::tools::ProcessLimitAcceptable(limit)) {
    abcd::tools::ReportLimitFailure(std::cerr, limit);
    return EXIT_FAILURE;
  }
  g_process_limit = &limit;
  const auto view = family_drafts ? ValueSourceScope::kFamilyDraft
      : conditional_families ? ValueSourceScope::kConditionalFamily
      : conditional_values ? ValueSourceScope::kConditionalValues : ValueSourceScope::kMember;
  if (views) return RunWithProcessReport(limit, [&] {
    return Inspect(argv[1], entity, std::nullopt, InspectDetail::kSummary, view);
  });
  if (instructions)
    return StreamWithProcessReport(limit,
        Inspect(argv[1], entity, std::nullopt, InspectDetail::kInstructions));
  // A bare --value-source keeps its historical meaning of the function form.
  if (value_routes && !form) form = source::SourceForm::kFunction;
  const auto numeric_contract = language_numeric ? source::SourceContract::kLanguageNumericV1 : language_arithmetic ? source::SourceContract::kLanguageArithmeticV1 : source::kSourceContract;
  return RunWithProcessReport(limit, [&] {
  return Inspect(argv[1], entity, form, InspectDetail::kSummary, print_budget?ValueSourceScope::kRecordPrintBudget:ValueSourceScope::kMember,
      language_source ? source::SourceContract::kLanguageSourceV8
      : language_constructors ? source::WithLanguageConstructors(numeric_contract) : numeric_contract);
  });
}
