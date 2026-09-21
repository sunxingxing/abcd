#include "environment_inspection.h"

#include <algorithm>

namespace abcd::tools {
namespace {
const char* Category(analysis::EnvironmentFlowError) { return "flow"; }
const char* Category(analysis::LexicalError) { return "lexical"; }
const char* Category(analysis::EnvironmentError) { return "environment"; }
}  // namespace

EnvironmentCounts& EnvironmentCounts::operator+=(
    const EnvironmentCounts& other) {
  methods += other.methods;
  rejected += other.rejected;
  allocations += other.allocations;
  bindings += other.bindings;
  captured_bindings += other.captured_bindings;
  instruction_visits += other.instruction_visits;
  peak_bytes = std::max(peak_bytes, other.peak_bytes);
  for (const auto& [error, count] : other.obstacles) obstacles[error] += count;
  return *this;
}

void InspectEnvironments(const abc::MethodBody& body,
                         const analysis::NormalFlow& graph,
                         std::uint64_t frame_size, EnvironmentCounts* counts) {
  const auto result = analysis::AnalyzeEnvironments(body, graph, frame_size);
  if (const auto* error =
          std::get_if<analysis::EnvironmentFlowFailure>(&result)) {
    ++counts->rejected;
    EnvironmentObstacle obstacle = error->kind;
    if (error->lexical_error) obstacle = *error->lexical_error;
    if (error->environment_error) obstacle = *error->environment_error;
    ++counts->obstacles[obstacle];
    return;
  }
  const auto& flow = std::get<analysis::EnvironmentFlow>(result);
  ++counts->methods;
  counts->allocations += flow.allocations.size();
  counts->bindings += flow.bindings.size();
  for (const auto& site : flow.bindings)
    if (std::holds_alternative<analysis::CapturedDepth>(
            site.binding.environment))
      ++counts->captured_bindings;
  counts->instruction_visits += flow.instruction_visits;
  counts->peak_bytes = std::max(counts->peak_bytes, flow.allocated_bytes);
}

nlohmann::json SerializeCounts(const EnvironmentCounts& counts) {
  auto obstacles = nlohmann::json::array();
  for (const auto& [error, count] : counts.obstacles)
    obstacles.push_back(std::visit(
        [&](auto kind) {
          return nlohmann::json{{"category", Category(kind)},
                                {"error", static_cast<int>(kind)},
                                {"count", count}};
        },
        error));
  return {{"environment_methods", counts.methods},
          {"environment_rejected", counts.rejected},
          {"environment_allocations", counts.allocations},
          {"lexical_bindings", counts.bindings},
          {"captured_bindings", counts.captured_bindings},
          {"environment_instruction_visits", counts.instruction_visits},
          {"environment_peak_bytes", counts.peak_bytes},
          {"environment_obstacles", std::move(obstacles)}};
}
}  // namespace abcd::tools
