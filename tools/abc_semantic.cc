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
#include <string_view>

#include "abcd/abc/metadata_index.h"
#include "abcd/abc/call_type.h"
#include "abcd/abc/frame_layout.h"
#include "abcd/source/value_stage.h"
#include "abcd/abc/method_body.h"
#include "abcd/abc/method_lookup.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/analysis/semantic_region.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/analysis/await_region.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/exception_edge.h"
#include "abcd/analysis/semantic_validation.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/semantic_source.h"
#include "abcd/source/semantic_entry.h"
#include "abcd/source/named_scope.h"
#include "abcd/source/record_program.h"
#include "abcd/source/source_route.h"
#include "abcd/abc/pool_reference.h"
#include "semantic_survey.h"
#include "exception_inspect.h"
#include "record_module.h"

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
};
enum class InspectDetail : std::uint8_t { kSummary, kInstructions, kExceptionFlow, kExceptionTemplate };
// Value-tree route receipts for methods the new pipeline cannot serve yet.
// Stage kDecoded is honest: this selector decodes the body to identify the
// method, then admission refuses the route before any analysis or emission;
// no value-tree stage ran, no legacy stage ran and no legacy text exists
// (A01). The failure is the admission decision's own typed product.
source::MethodRouteReceipt ValueRouteReceipt(
    const source::RouteInputIdentity& identity,
    const source::RouteFailure& failure, std::uint32_t entity,
    std::optional<std::uint32_t> record) {
  source::MethodRouteReceipt receipt;
  receipt.identity = identity;
  receipt.method = abc::EntityOffset{entity};
  if (record) receipt.record = abc::EntityOffset{*record};
  receipt.route = source::SourceRoute::kValueTree;
  receipt.stage = source::SourceStage::kDecoded;
  receipt.coverage = source::Coverage::kUnsupported;
  receipt.legacy_writer_used = false;
  receipt.failure = failure;
  return receipt;
}
int ReportRouteReceipt(const source::MethodRouteReceipt& receipt,
                       const analysis::ValueProgram* graph = nullptr) {
  const auto serialized = source::SerializeRouteReceipt(receipt);
  const auto* json = std::get_if<std::string>(&serialized);
  if (!json) {
    std::cerr << "invalid route receipt: "
              << static_cast<unsigned>(
                     std::get<source::RouteReceiptError>(serialized)) << '\n';
    return EXIT_FAILURE;
  }
  // The receipt is the requested product; a non-Complete coverage is still a
  // non-success exit so automation cannot mistake refusal for recovery.
  nlohmann::json report{
      {"route_receipt", nlohmann::json::parse(*json)},
      {"source_complete", false},
      {"executable", false}};
  if (graph) report["value_graph"] = {
      {"operations", graph->ops.size()}, {"values", graph->values.size()},
      {"uses", graph->uses.size()}, {"blocks", graph->blocks.size()},
      {"edges", graph->edges.size()},
      {"exceptional_exits", graph->exceptional_exits.size()}};
  std::cout << report << '\n';
  if (!std::cout) return EXIT_FAILURE;
  return receipt.coverage == source::Coverage::kComplete &&
                 !receipt.failure
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
int Fail(InspectError kind, std::uint32_t pc = 0) {
  std::cerr << nlohmann::json{{"error", static_cast<unsigned>(kind)},
                              {"pc", pc}}
            << '\n';
  return EXIT_FAILURE;
}
int FailStage(InspectError kind, unsigned stage_error,
              std::uint32_t pc) {
  std::cerr << nlohmann::json{{"error", static_cast<unsigned>(kind)},
                              {"stage_error", stage_error},
                              {"pc", pc}}
            << '\n';
  return EXIT_FAILURE;
}
int Inspect(const std::filesystem::path& path, std::uint32_t entity,
            std::optional<source::SourceForm> form, bool all_records,
            InspectDetail detail, source::RoutePolicy policy) {
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
  constexpr std::size_t kIndexBytes = 256 * 1024 * 1024;
  const auto indexed = abc::BuildMetadataIndex(file, *header, {kIndexBytes});
  const auto* index = std::get_if<abc::MetadataIndex>(&indexed);
  if (!index) return Fail(InspectError::kIndex);
  if (all_records) return SurveyRecords(file, *header, *index, *hash, std::cout);
  const auto method = std::find_if(
      index->methods.begin(), index->methods.end(),
      [entity](const abc::MethodInfo& item) {
        return item.offset.value == entity;
      });
  if (method == index->methods.end() || !method->code)
    return Fail(InspectError::kMethod);
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
  // The required route consumes input analysis but never enters the legacy
  // region/AST/printer path. T01 publishes intermediate evidence, not source.
  if (policy == source::RoutePolicy::kRequireValueTree) {
    const auto call = abc::ReadCallType(file, *header, *index, method->annotations);
    const auto* call_type = std::get_if<abc::CallType>(&call);
    if (!call_type) return Fail(InspectError::kFrame);
    const auto layout = abc::BuildFrameLayout(code.registers, code.arguments,
                                              abc::DecodeCallFlags(call_type->value));
    const auto* frame = std::get_if<abc::FrameLayout>(&layout);
    if (!frame) return Fail(InspectError::kFrame);
    auto stage = source::BuildValueStage(source::MakeRouteInputIdentity(*hash, *header),
        method->offset, method->owner_offset, *body, *frame);
    if (file.CheckUnchanged()) return Fail(InspectError::kBody);
    return ReportRouteReceipt(stage.receipt, stage.graph ? &*stage.graph : nullptr);
  }
  if (detail == InspectDetail::kExceptionFlow || detail == InspectDetail::kExceptionTemplate) {
    // Child templates need the same-file membership and role owners used by
    // record emission. These do not grant module/global/capture premises.
    const auto lookup = abc::MethodLookup::Build(index->methods);
    const auto* methods = std::get_if<abc::MethodLookup>(&lookup);
    if (!methods) return Fail(InspectError::kIndex);
    const auto role_index = analysis::MethodRoleIndex::Build(file, *header, *index, *methods);
    const auto* roles = std::get_if<analysis::MethodRoleIndex>(&role_index);
    source::MethodConstants constants{file, *header, *index,
                                            method->offset, abc::StringBudget{}};
    constants.leaf_methods = methods;
    constants.roles = roles;
    const auto packaging = abc::DetectFilePackaging(file, *header, *index);
    const auto* layout = std::get_if<abc::FilePackaging>(&packaging);
    if (!layout) return Fail(InspectError::kIndex);
    std::optional<RecordModule> bindings;
    if (*layout == abc::FilePackaging::kMerged) {
      const auto position = static_cast<std::size_t>(method - index->methods.begin());
      const auto record = std::find_if(index->records.begin(), index->records.end(),
          [&](const auto& owner) {
            return position >= owner.first_method &&
                   position - owner.first_method < owner.method_count;
          });
      if (record == index->records.end()) return Fail(InspectError::kIndex);
      auto loaded = ReadRecordModule(file, *header, *index, *record);
      auto* module = std::get_if<std::optional<RecordModule>>(&loaded);
      if (!module) return Fail(InspectError::kIndex);
      bindings = std::move(*module);
      if (bindings) {
        constants.module = &bindings->module;
        constants.global_scope.emplace();
        constants.global_scope->module_slots = bindings->exports.slot_count();
        constants.global_scope->module_exports = &bindings->exports;
      }
    }
    return InspectExceptionFlow(*body, header->profile,
                                 code.registers + code.arguments, code.registers,
                                 constants, std::cout, detail == InspectDetail::kExceptionTemplate
                                     ? analysis::CapturedValues::kLanguage : analysis::CapturedValues::kReject);
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
        output["semantic_error"] = static_cast<unsigned>(
            std::get<analysis::SemanticFailure>(semantic).kind);
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
            {"kind", static_cast<unsigned>(failure.kind)},
            {"pc", failure.pc.value}};
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
    std::cerr << nlohmann::json{
                     {"error", static_cast<unsigned>(InspectError::kSemantic)},
                     {"semantic_error", static_cast<unsigned>(failure.kind)},
                     {"pc", failure.pc.value}}
              << '\n';
    return EXIT_FAILURE;
  }
  if (form) {
    constexpr std::size_t kMethodBytes = 64 * 1024 * 1024;
    source::MethodConstants constants{file, *header, *index, method->offset,
                                      {kMethodBytes}};
    if (method_ir->allocated_bytes > kMethodBytes)
      return Fail(InspectError::kIndex);
    const auto lookup = abc::MethodLookup::Build(
        index->methods, {kMethodBytes - method_ir->allocated_bytes});
    const auto* methods = std::get_if<abc::MethodLookup>(&lookup);
    if (!methods) return Fail(InspectError::kIndex);
    constants.leaf_methods = methods;
    auto available = kMethodBytes - methods->AllocatedBytes();
    // Nested arrow/template emission needs the complete same-file role proof,
    // just like record and exception owners. Never treat a missing index as
    // kUnseen or infer roles from the inspected function alone.
    const auto role_index = analysis::MethodRoleIndex::Build(
        file, *header, *index, *methods,
        {available - method_ir->allocated_bytes});
    const auto* roles = std::get_if<analysis::MethodRoleIndex>(&role_index);
    if (roles) {
      if (roles->AllocatedBytes() > available - method_ir->allocated_bytes)
        return Fail(InspectError::kIndex);
      available -= roles->AllocatedBytes();
      constants.roles = roles;
    }
    // Reserve the complete module context, including export-name decoding
    // scratch, alongside the live IR/lookup/roles. Planning is not execution:
    // these slots remain uninitialized in detached function inspection.
    constexpr std::size_t kRecordContextBytes = 16 * 1024 * 1024;
    constexpr std::size_t kModuleTableBytes = kRecordContextBytes / 2;
    constexpr std::size_t kExportPlanBytes = kRecordContextBytes / 4;
    constexpr std::size_t kExportStringBytes =
        kRecordContextBytes - kModuleTableBytes - kExportPlanBytes;
    if (kRecordContextBytes > available - method_ir->allocated_bytes)
      return Fail(InspectError::kIndex);
    const auto packaging = abc::DetectFilePackaging(
        file, *header, *index, {kRecordContextBytes});
    const auto* layout = std::get_if<abc::FilePackaging>(&packaging);
    if (!layout) return Fail(InspectError::kIndex);
    std::optional<RecordModule> bindings;
    const std::array wrapper_bindings{std::string_view(method->name)};
    if (*layout == abc::FilePackaging::kMerged) {
      const auto position = static_cast<std::size_t>(method - index->methods.begin());
      const auto record = std::find_if(index->records.begin(), index->records.end(),
          [&](const auto& owner) {
            return owner.offset == method->owner_offset &&
                   position >= owner.first_method &&
                   position - owner.first_method < owner.method_count;
          });
      if (record == index->records.end()) return Fail(InspectError::kIndex);
      if (*form == source::SourceForm::kRecord) {
        // Pinned translator wire entry name, not instruction/string dispatch.
        constexpr std::string_view kEntryName = "func_main_0";
        if (method->name != kEntryName ||
            !source::SupportsRecordProgramProfile(header->profile) ||
            record->first_method > index->methods.size() ||
            record->method_count > index->methods.size() - record->first_method)
          return Fail(InspectError::kMethod);
        const auto entries = std::count_if(
            index->methods.begin() + record->first_method,
            index->methods.begin() + record->first_method + record->method_count,
            [](const auto& item) { return item.name == kEntryName; });
        if (entries != 1) return Fail(InspectError::kMethod);
        const auto attributes = abc::ReadMergedRecordAttributes(
            file, *header, *index, *record, {kRecordContextBytes});
        const auto* flags = std::get_if<abc::RecordAttributes>(&attributes);
        if (!flags || flags->common_js.value_or(false) ||
            flags->shared.value_or(false) || flags->top_level_await.value_or(false))
          return Fail(InspectError::kMethod);
      }
      auto loaded = ReadRecordModule(file, *header, *index, *record,
          {{kModuleTableBytes}, {kExportPlanBytes, {kExportStringBytes}},
           {kRecordContextBytes}});
      auto* module = std::get_if<std::optional<RecordModule>>(&loaded);
      if (!module) return Fail(InspectError::kIndex);
      bindings = std::move(*module);
      if (bindings) {
        available -= kRecordContextBytes;
        constants.module = &bindings->module;
        constants.global_scope.emplace();
        if (*form != source::SourceForm::kRecord)
          constants.global_scope->enclosing_bindings = wrapper_bindings;
        constants.global_scope->module_slots = bindings->exports.slot_count();
        constants.global_scope->module_exports = &bindings->exports;
      }
    }
    if (*form == source::SourceForm::kRecord && !bindings)
      return Fail(InspectError::kMethod);
    const auto call = abc::ReadCallType(file, *header, *index,
                                        method->annotations);
    const auto* call_type = std::get_if<abc::CallType>(&call);
    if (!call_type) return Fail(InspectError::kFrame);
    const auto built_frame = abc::BuildFrameLayout(
        code.registers, code.arguments,
        abc::DecodeCallFlags(call_type->value));
    const auto* frame = std::get_if<abc::FrameLayout>(&built_frame);
    if (!frame) return Fail(InspectError::kFrame);
    const auto rendered = source::BuildEntrySource(
        *body, *method_ir, *frame, *form, method->name, constants,
        *form == source::SourceForm::kRecord ? &bindings->exports : nullptr,
        {available}, kMethodBytes, policy);
    const auto* printed = std::get_if<source::PrintedSource>(&rendered);
    if (!printed) {
      return std::visit([&](const auto& failure) -> int {
        using T = std::decay_t<decltype(failure)>;
        if constexpr (std::is_same_v<T, source::PrintedSource>) {
          std::terminate();
        } else if constexpr (std::is_same_v<T, source::RouteRefusal>) {
          // Defense in depth: admission already refused above, so this is
          // unreachable today. Consume the typed failure rather than
          // re-deriving it (A01).
          return ReportRouteReceipt(ValueRouteReceipt(
              source::MakeRouteInputIdentity(*hash, *header),
              failure.failure, entity, method->owner_offset.value));
        } else {
          constexpr auto stage = [] {
            if constexpr (std::is_same_v<T, analysis::SemanticFailure>)
              return InspectError::kSemantic;
            else if constexpr (std::is_same_v<T, analysis::SemanticProofFailure>)
              return InspectError::kProof;
            else if constexpr (std::is_same_v<T, analysis::RegionFailure>)
              return InspectError::kRegion;
            else return InspectError::kPrint;
          }();
          return FailStage(stage, static_cast<unsigned>(failure.kind), failure.pc.value);
        }
      }, rendered);
    }
    nlohmann::json map = nlohmann::json::array();
    for (const auto& span : printed->spans)
      map.push_back({{"pc", span.pc.value},
                     {"origin_kind", source::SpanOriginName(span.origin)},
                     {"begin", {span.begin.byte, span.begin.line,
                                 span.begin.column}},
                     {"end", {span.end.byte, span.end.line,
                               span.end.column}}});
    std::cout << nlohmann::json{{"entity", entity},
                                {"source_route", source::SourceRouteName(source::SourceRoute::kLegacy)},
                                {"source_contract", source::SourceContractName(printed->contract)},
                                {"host_realm_binding", printed->host_realm
                                    ? nlohmann::json(printed->host_realm->binding) : nlohmann::json(nullptr)},
                                {"source_complete", false},
                                {"executable", false},
                                {"requires_module_bindings", bindings.has_value() &&
                                    *form != source::SourceForm::kRecord},
                                {"source", printed->text},
                                {"positions", std::move(map)}}
              << '\n';
    return std::cout ? EXIT_SUCCESS : EXIT_FAILURE;
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
  if (argc < 3 || argc > 5) {
    std::cerr << "usage: abc_semantic INPUT.abc METHOD_ENTITY [--class-source|--function-source|--record-source] [--value-source]\n"
                 "       abc_semantic INPUT.abc METHOD_ENTITY --instructions|--exception-flow|--exception-template\n"
                 "       abc_semantic INPUT.abc --all-records\n"
                 "  exactly one source form per invocation; duplicates, conflicting forms\n"
                 "  and any combination of a detail mode with --value-source are rejected\n"
                 "  regardless of option order\n"
                 "  --value-source requires the unified value-tree route; stages that are\n"
                 "  not implemented return a typed route receipt with no source text and\n"
                 "  never fall back to the legacy writer\n";
    return Fail(InspectError::kArguments);
  }
  std::uint32_t entity = 0;
  const std::string_view token{argv[2]};
  if (token == "--all-records" && argc == 3)
    return Inspect(argv[1], 0, std::nullopt, true, InspectDetail::kSummary,
                   source::RoutePolicy::kLegacyAllowed);
  const auto number = std::from_chars(
      token.data(), token.data() + token.size(), entity);
  if (number.ec != std::errc{} ||
      number.ptr != token.data() + token.size())
    return Fail(InspectError::kArguments);
  // Parse every option first, then validate combinations independent of
  // order before any file access (T00 correction R1): an earlier-accepted
  // option can no longer mute a later --value-source.
  std::optional<source::SourceForm> form;
  InspectDetail detail = InspectDetail::kSummary;
  unsigned forms = 0;
  unsigned details = 0;
  unsigned value_routes = 0;
  for (int i = 3; i < argc; ++i) {
    const std::string_view option{argv[i]};
    if (option == "--value-source") {
      ++value_routes;
    } else if (option == "--instructions" ||
               option == "--exception-flow" ||
               option == "--exception-template") {
      ++details;
      detail = option == "--instructions" ? InspectDetail::kInstructions
              : option == "--exception-flow" ? InspectDetail::kExceptionFlow
                                             : InspectDetail::kExceptionTemplate;
    } else if (option == "--class-source") {
      ++forms;
      form = source::SourceForm::kClassConstructor;
    } else if (option == "--function-source") {
      ++forms;
      form = source::SourceForm::kFunction;
    } else if (option == "--record-source") {
      ++forms;
      form = source::SourceForm::kRecord;
    } else {
      return Fail(InspectError::kArguments);
    }
  }
  // Duplicate or conflicting selectors are rejected in every order.
  if (forms > 1 || details > 1 || value_routes > 1 || (forms && details))
    return Fail(InspectError::kArguments);
  // Raw inspection modes predate route receipts; do not mix them with a
  // route policy until those views carry receipts too.
  if (details && value_routes) return Fail(InspectError::kArguments);
  if (details)
    return Inspect(argv[1], entity, std::nullopt, false, detail,
                   source::RoutePolicy::kLegacyAllowed);
  const auto policy = value_routes
                          ? source::RoutePolicy::kRequireValueTree
                          : source::RoutePolicy::kLegacyAllowed;
  return Inspect(argv[1], entity, form, false, InspectDetail::kSummary, policy);
}
