#ifndef ABCD_TOOLS_ENVIRONMENT_INSPECTION_H_
#define ABCD_TOOLS_ENVIRONMENT_INSPECTION_H_

#include <map>
#include <nlohmann/json.hpp>

#include "abcd/analysis/environment_flow.h"

namespace abcd::tools {
using EnvironmentObstacle =
    std::variant<analysis::EnvironmentFlowError, analysis::LexicalError,
                 analysis::EnvironmentError>;
struct EnvironmentCounts {
  std::uint64_t methods = 0;
  std::uint64_t rejected = 0;
  std::uint64_t allocations = 0;
  std::uint64_t bindings = 0;
  std::uint64_t captured_bindings = 0;
  std::uint64_t instruction_visits = 0;
  std::size_t peak_bytes = 0;
  std::map<EnvironmentObstacle, std::uint64_t> obstacles;
  EnvironmentCounts& operator+=(const EnvironmentCounts& other);
};
void InspectEnvironments(const abc::MethodBody& body,
                         const analysis::NormalFlow& graph,
                         std::uint64_t frame_size, EnvironmentCounts* counts);
nlohmann::json SerializeCounts(const EnvironmentCounts& counts);
}  // namespace abcd::tools
#endif
