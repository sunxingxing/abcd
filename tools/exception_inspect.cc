#include "exception_inspect.h"

#include <cstdlib>
#include <nlohmann/json.hpp>
#include "abcd/analysis/exception_flow.h"

int InspectExceptionFlow(const abcd::abc::MethodBody& body,
    abcd::abc::ProfileId profile, std::uint64_t frame_size, std::ostream& output) {
  using namespace abcd;
  const auto built = analysis::BuildExceptionFlow(body, profile, frame_size);
  const auto* flow = std::get_if<analysis::ExceptionFlow>(&built);
  if (!flow) {
    if (const auto* failure = std::get_if<analysis::SemanticFailure>(&built))
      output << nlohmann::json{{"semantic_error", static_cast<unsigned>(failure->kind)},
                               {"pc", failure->pc.value}} << '\n';
    else if (const auto* failure = std::get_if<analysis::FlowFailure>(&built))
      output << nlohmann::json{{"flow_error", static_cast<unsigned>(failure->kind)},
                               {"pc", failure->offset.value}} << '\n';
    else {
      const auto& dispatch = std::get<analysis::DispatchFailure>(built);
      output << nlohmann::json{{"dispatch_error", static_cast<unsigned>(dispatch.kind)},
                               {"pc", dispatch.offset.value}} << '\n';
    }
    return EXIT_FAILURE;
  }
  output << nlohmann::json{{"schema", 1}, {"instructions", flow->instructions.size()},
      {"nodes", flow->nodes.size()}, {"source_complete", false}, {"ssa_complete", false}}
      << '\n';
  for (std::size_t i = 0; i < flow->nodes.size(); ++i) {
    const auto& node = flow->nodes[i];
    const auto& instruction = flow->instructions[node.instruction.value];
    nlohmann::json row{{"node", i}, {"instruction", node.instruction.value},
        {"pc", instruction.pc.value}, {"phase", static_cast<unsigned>(node.phase)},
        {"successors", nlohmann::json::array()}};
    for (std::size_t e = 0; e < node.successor_count; ++e)
      row["successors"].push_back({{"target", node.successors[e].target.value},
          {"kind", static_cast<unsigned>(node.successors[e].kind)}});
    if (node.phase == analysis::ExceptionPhase::kCommit &&
        instruction.effects.normal_registers.write) {
      const auto& slot = *instruction.effects.normal_registers.write;
      if (const auto* reg = std::get_if<abc::RegisterId>(&slot))
        row["register_write"] = reg->value;
      else row["accumulator_write"] = true;
    }
    if (node.phase == analysis::ExceptionPhase::kCatch)
      row["catch_accumulator"] = true;
    if (node.exception)
      row["exceptional_completion"] = static_cast<unsigned>(node.exception->completion);
    if (node.exception && !node.exception->target)
      row["propagates_to_caller"] = true;
    output << row << '\n';
  }
  return output ? EXIT_SUCCESS : EXIT_FAILURE;
}
