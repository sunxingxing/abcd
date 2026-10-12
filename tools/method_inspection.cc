#include "method_inspection.h"

#include <algorithm>
#include <cstdlib>
#include <ostream>

#include "abcd/abc/method_body.h"
#include "abcd/abc/call_type.h"
#include "abcd/abc/frame_layout.h"
#include "abcd/analysis/exception_dispatch.h"
#include "abcd/analysis/normal_flow.h"
#include "abcd/analysis/dominators.h"
#include "reference_inspection.h"

namespace abcd::tools {
namespace {
void WriteFailure(std::ostream& output, const abc::MethodInfo& method,
                  const abc::BodyFailure& failure) {
  nlohmann::json diagnostic{
      {"method_offset", method.offset.value},
      {"code_offset", method.code->offset.value},
      {"pc", failure.offset.value},
      {"kind", static_cast<int>(failure.kind)},
      {"stage", StageLabel(InspectionStage::kInstructions)}};
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

void WriteFailure(std::ostream& output, const abc::MethodInfo& method,
                  const analysis::DispatchFailure& failure) {
  output << nlohmann::json{
      {"method_offset", method.offset.value},
      {"code_offset", method.code->offset.value},
      {"pc", failure.offset.value},
      {"kind", static_cast<int>(failure.kind)},
      {"stage", StageLabel(InspectionStage::kExceptionDispatch)}};
}

bool BlockEntry(const analysis::NormalFlow& graph, abc::CodeOffset pc) {
  const auto found = std::lower_bound(
      graph.blocks.begin(), graph.blocks.end(), pc,
      [](const analysis::FlowBlock& block, abc::CodeOffset offset) {
        return block.begin < offset;
      });
  return found != graph.blocks.end() && found->begin == pc;
}

std::optional<analysis::DispatchFailure> CheckDispatch(
    const analysis::ExceptionDispatch& dispatch,
    const analysis::NormalFlow& graph, MethodCounts* counts) {
  // Every selected interval and handler entry must agree with the normal
  // graph's boundaries. This is not a claim that all protected instructions
  // throw: effects and pre-throw register state belong to semantic recovery.
  for (const auto& span : dispatch.spans) {
    if (!BlockEntry(graph, span.begin) ||
        (span.end != graph.blocks.back().end && !BlockEntry(graph, span.end)))
      return analysis::DispatchFailure{
          analysis::DispatchError::kInvalidBoundary, span.begin};
    if (!BlockEntry(graph, span.target.handler_pc))
      return analysis::DispatchFailure{
          analysis::DispatchError::kInvalidBoundary, span.target.handler_pc};
  }
  for (const auto& block : graph.blocks) {
    if (dispatch.Find(block.begin)) {
      ++counts->protected_blocks;
      counts->protected_instructions += block.instruction_count;
    }
  }
  return std::nullopt;
}
}  // namespace

const char* StageLabel(InspectionStage stage) {
  switch (stage) {
    case InspectionStage::kMetadata:
      return "metadata";
    case InspectionStage::kInstructions:
      return "method_encoding";
    case InspectionStage::kNormalFlow:
      return "normal_flow";
    case InspectionStage::kExceptionDispatch:
      return "exception_dispatch";
    case InspectionStage::kReferences:
      return "references";
    case InspectionStage::kModules:
      return "modules";
    case InspectionStage::kDominators:
      return "normal_dominators";
    case InspectionStage::kFrames:
      return "frames";
    case InspectionStage::kEnvironments:
      return "environment_bindings";
    case InspectionStage::kCreations:
      return "creation_bindings";
  }
  std::abort();
}

MethodCounts& MethodCounts::operator+=(const MethodCounts& other) {
  creations += other.creations;
  failed_creation_indexes += other.failed_creation_indexes;
  environments += other.environments;
  for (std::size_t i = 0; i < function_kinds.size(); ++i)
    function_kinds[i] += other.function_kinds[i];
  sendable_methods += other.sendable_methods;
  frame_methods += other.frame_methods;
  failed_frame_methods += other.failed_frame_methods;
  default_call_type_methods += other.default_call_type_methods;
  hidden_slots += other.hidden_slots;
  user_parameters += other.user_parameters;
  extra_argument_methods += other.extra_argument_methods;
  code_methods += other.code_methods;
  instruction_bytes += other.instruction_bytes;
  decoded_methods += other.decoded_methods;
  failed_methods += other.failed_methods;
  instructions += other.instructions;
  try_regions += other.try_regions;
  catch_handlers += other.catch_handlers;
  flow_methods += other.flow_methods;
  failed_flow_methods += other.failed_flow_methods;
  blocks += other.blocks;
  edges += other.edges;
  resume_edges += other.resume_edges;
  dispatch_methods += other.dispatch_methods;
  failed_dispatch_methods += other.failed_dispatch_methods;
  dispatch_spans += other.dispatch_spans;
  protected_blocks += other.protected_blocks;
  protected_instructions += other.protected_instructions;
  reference_methods += other.reference_methods;
  failed_reference_methods += other.failed_reference_methods;
  reference_operands += other.reference_operands;
  unique_references += other.unique_references;
  string_literals += other.string_literals;
  literal_arrays += other.literal_arrays;
  literal_entries += other.literal_entries;
  dominator_methods += other.dominator_methods;
  failed_dominator_methods += other.failed_dominator_methods;
  reachable_blocks += other.reachable_blocks;
  dominator_edge_visits += other.dominator_edge_visits;
  dominator_eval_steps += other.dominator_eval_steps;
  dominator_peak_bytes = std::max(dominator_peak_bytes, other.dominator_peak_bytes);
  return *this;
}

std::uint64_t MethodCounts::FailureCount() const {
  return failed_creation_indexes + failed_methods + failed_flow_methods + failed_dispatch_methods +
         failed_reference_methods + failed_dominator_methods + failed_frame_methods;
}

nlohmann::json SerializeCounts(const MethodCounts& counts) {
  nlohmann::json result{{"function_kinds", counts.function_kinds},
          {"sendable_methods", counts.sendable_methods},
          {"frame_methods", counts.frame_methods},
          {"failed_frame_methods", counts.failed_frame_methods},
          {"default_call_type_methods", counts.default_call_type_methods},
          {"hidden_slots", counts.hidden_slots},
          {"user_parameters", counts.user_parameters},
          {"extra_argument_methods", counts.extra_argument_methods},
          {"code_methods", counts.code_methods},
          {"instruction_bytes", counts.instruction_bytes},
          {"decoded_methods", counts.decoded_methods},
          {"failed_methods", counts.failed_methods},
          {"instructions", counts.instructions},
          {"try_regions", counts.try_regions},
          {"catch_handlers", counts.catch_handlers},
          {"flow_methods", counts.flow_methods},
          {"failed_flow_methods", counts.failed_flow_methods},
          {"blocks", counts.blocks},
          {"edges", counts.edges},
          {"resume_edges", counts.resume_edges},
          {"dispatch_methods", counts.dispatch_methods},
          {"failed_dispatch_methods", counts.failed_dispatch_methods},
          {"dispatch_spans", counts.dispatch_spans},
          {"protected_blocks", counts.protected_blocks},
          {"protected_instructions", counts.protected_instructions},
          {"reference_methods", counts.reference_methods},
          {"failed_reference_methods", counts.failed_reference_methods},
          {"reference_operands", counts.reference_operands},
          {"unique_references", counts.unique_references},
          {"string_literals", counts.string_literals},
          {"literal_arrays", counts.literal_arrays},
          {"literal_entries", counts.literal_entries},
          {"dominator_methods", counts.dominator_methods},
          {"failed_dominator_methods", counts.failed_dominator_methods},
          {"reachable_blocks", counts.reachable_blocks},
          {"dominator_edge_visits", counts.dominator_edge_visits},
          {"dominator_eval_steps", counts.dominator_eval_steps},
          {"dominator_peak_bytes", counts.dominator_peak_bytes}};
  result.update(SerializeCounts(counts.environments));
  result.update(SerializeCounts(counts.creations));
  result["failed_creation_indexes"] = counts.failed_creation_indexes;
  return result;
}

MethodCounts InspectMethods(const corpus::AbcFile& file,
                            const corpus::Sha256& fingerprint,
                            const abc::Header& header,
                            const abc::MetadataIndex& index,
                            InspectionStage stage, std::ostream& diagnostics) {
  MethodCounts counts;
  std::optional<abc::MethodLookup> lookup;
  if (stage == InspectionStage::kCreations) {
    auto built = abc::MethodLookup::Build(index.methods);
    if (const auto* error = std::get_if<abc::MethodLookupError>(&built)) {
      ++counts.failed_creation_indexes;
      diagnostics << nlohmann::json{{"stage", StageLabel(stage)},
                                    {"lookup_error", static_cast<int>(*error)}};
      return counts;
    }
    lookup.emplace(std::move(std::get<abc::MethodLookup>(built)));
  }
  for (const auto& method : index.methods) {
    if (!method.code) continue;
    ++counts.code_methods;
    counts.instruction_bytes += method.code->instruction_bytes;
    if (stage == InspectionStage::kMetadata) continue;
    if (stage == InspectionStage::kFrames) {
      const auto call_type = abc::ReadCallType(file, header, index, method.annotations);
      const auto fail = [&](const char* category, int error) {
        if (counts.FailureCount() != 0) diagnostics << ',';
        diagnostics << nlohmann::json{
            {"method_offset", method.offset.value},
            {"code_offset", method.code->offset.value},
            {"stage", StageLabel(stage)}, {category, error}};
        ++counts.failed_frame_methods;
      };
      if (const auto* error = std::get_if<abc::MetadataError>(&call_type)) {
        fail("metadata_error", static_cast<int>(*error));
        continue;
      }
      const auto value = std::get<abc::CallType>(call_type).value;
      const auto attributes = abc::DecodeMethodAttributes(method.access_flags, header.profile);
      if (const auto* error = std::get_if<abc::MethodAttributeError>(&attributes)) {
        fail("attribute_error", static_cast<int>(*error));
        continue;
      }
      const auto layout = abc::BuildFrameLayout(
          method.code->registers, method.code->arguments, abc::DecodeCallFlags(value));
      if (const auto* error = std::get_if<abc::FrameError>(&layout)) {
        fail("frame_error", static_cast<int>(*error));
        continue;
      }
      const auto& frame = std::get<abc::FrameLayout>(layout);
      ++counts.frame_methods;
      const auto& flags = std::get<abc::MethodAttributes>(attributes);
      ++counts.function_kinds[static_cast<std::size_t>(flags.kind)];
      if (flags.sendable) ++counts.sendable_methods;
      if (!value) ++counts.default_call_type_methods;
      counts.hidden_slots += frame.first_parameter.value - frame.locals;
      counts.user_parameters += frame.parameters;
      if (frame.extra_arguments) ++counts.extra_argument_methods;
      continue;
    }
    const auto decoded =
        abc::DecodeMethodBody(file, header.profile, *method.code);
    if (const auto* failure = std::get_if<abc::BodyFailure>(&decoded)) {
      // Stream every failure rather than retaining or truncating diagnostics.
      if (counts.FailureCount() != 0) diagnostics << ',';
      WriteFailure(diagnostics, method, *failure);
      ++counts.failed_methods;
      continue;
    }
    const auto& body = std::get<abc::MethodBody>(decoded);
    ++counts.decoded_methods;
    counts.instructions += body.instructions.size();
    counts.try_regions += body.tries.size();
    counts.catch_handlers += body.handlers.size();
    if (stage == InspectionStage::kInstructions) continue;
    const auto flow = analysis::BuildNormalFlow(body);
    if (const auto* failure = std::get_if<analysis::FlowFailure>(&flow)) {
      if (counts.FailureCount() != 0) diagnostics << ',';
      WriteFailure(diagnostics, method, *failure);
      ++counts.failed_flow_methods;
      continue;
    }
    ++counts.flow_methods;
    const auto& graph = std::get<analysis::NormalFlow>(flow);
    counts.blocks += graph.blocks.size();
    for (const auto& block : graph.blocks) {
      counts.edges += block.successor_count;
      for (const auto& edge :
           std::span(block.successors).first(block.successor_count))
        if (edge.kind == analysis::FlowEdgeKind::kResume) ++counts.resume_edges;
    }
    if (stage == InspectionStage::kNormalFlow) continue;
    if (stage == InspectionStage::kCreations) {
      InspectCreations(file, header, index, *lookup, fingerprint, method, body,
                       graph, &counts.creations);
      continue;
    }
    if (stage == InspectionStage::kEnvironments) {
      InspectEnvironments(body, graph,
                          std::uint64_t(method.code->registers) + method.code->arguments,
                          &counts.environments);
      continue;
    }
    if (stage == InspectionStage::kDominators) {
      const auto built = analysis::BuildNormalDominators(graph);
      if (const auto* error = std::get_if<analysis::DominatorError>(&built)) {
        if (counts.FailureCount() != 0) diagnostics << ',';
        diagnostics << nlohmann::json{
            {"method_offset", method.offset.value},
            {"code_offset", method.code->offset.value},
            {"kind", static_cast<int>(*error)},
            {"stage", StageLabel(stage)}};
        ++counts.failed_dominator_methods;
      } else {
        const auto& tree = std::get<analysis::DominatorTree>(built);
        ++counts.dominator_methods;
        for (const auto& node : tree.nodes)
          if (node.begin != 0) ++counts.reachable_blocks;
        counts.dominator_edge_visits += tree.work.edge_visits;
        counts.dominator_eval_steps += tree.work.eval_steps;
        counts.dominator_peak_bytes =
            std::max(counts.dominator_peak_bytes, tree.work.allocated_bytes);
      }
      continue;
    }
    const auto dispatched =
        analysis::BuildExceptionDispatch(body, header.profile);
    if (const auto* failure =
            std::get_if<analysis::DispatchFailure>(&dispatched)) {
      if (counts.FailureCount() != 0) diagnostics << ',';
      WriteFailure(diagnostics, method, *failure);
      ++counts.failed_dispatch_methods;
      continue;
    }
    const auto& dispatch = std::get<analysis::ExceptionDispatch>(dispatched);
    if (const auto failure = CheckDispatch(dispatch, graph, &counts)) {
      if (counts.FailureCount() != 0) diagnostics << ',';
      WriteFailure(diagnostics, method, *failure);
      ++counts.failed_dispatch_methods;
      continue;
    }
    ++counts.dispatch_methods;
    counts.dispatch_spans += dispatch.spans.size();
    if (stage == InspectionStage::kExceptionDispatch) continue;
    if (const auto failure =
            InspectReferences(file, header, index, method, body, &counts)) {
      if (counts.FailureCount() != 0) diagnostics << ',';
      diagnostics << *failure;
      ++counts.failed_reference_methods;
      continue;
    }
    ++counts.reference_methods;
  }
  return counts;
}

}  // namespace abcd::tools
