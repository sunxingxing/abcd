#include "method_inspection.h"

#include <algorithm>
#include <cstdlib>
#include <ostream>

#include "abcd/abc/method_body.h"
#include "abcd/analysis/exception_dispatch.h"
#include "abcd/analysis/normal_flow.h"

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
  }
  std::abort();
}

MethodCounts& MethodCounts::operator+=(const MethodCounts& other) {
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
  return *this;
}

std::uint64_t MethodCounts::FailureCount() const {
  return failed_methods + failed_flow_methods + failed_dispatch_methods;
}

nlohmann::json SerializeCounts(const MethodCounts& counts) {
  return {{"code_methods", counts.code_methods},
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
          {"protected_instructions", counts.protected_instructions}};
}

MethodCounts InspectMethods(const corpus::AbcFile& file, abc::ProfileId profile,
                            std::span<const abc::MethodInfo> methods,
                            InspectionStage stage, std::ostream& diagnostics) {
  MethodCounts counts;
  for (const auto& method : methods) {
    if (!method.code) continue;
    ++counts.code_methods;
    counts.instruction_bytes += method.code->instruction_bytes;
    if (stage == InspectionStage::kMetadata) continue;
    const auto decoded = abc::DecodeMethodBody(file, profile, *method.code);
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
    const auto dispatched = analysis::BuildExceptionDispatch(body, profile);
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
  }
  return counts;
}

}  // namespace abcd::tools
