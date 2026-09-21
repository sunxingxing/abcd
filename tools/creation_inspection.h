#ifndef ABCD_TOOLS_CREATION_INSPECTION_H_
#define ABCD_TOOLS_CREATION_INSPECTION_H_

#include <map>
#include <nlohmann/json.hpp>

#include "abcd/analysis/creation_binding.h"

namespace abcd::tools {
using CreationObstacle =
    std::variant<analysis::EnvironmentFlowError, analysis::LexicalError,
                 analysis::EnvironmentError, analysis::CreationError,
                 analysis::CreationBindingError, abc::MethodTargetError,
                 abc::MetadataError, abc::MethodAttributeError>;
struct CreationCounts {
  std::uint64_t methods = 0;
  std::uint64_t rejected_methods = 0;
  std::uint64_t sites = 0;
  std::uint64_t bound = 0;
  std::uint64_t failed = 0;
  std::uint64_t unreachable = 0;
  std::uint64_t local_captures = 0;
  std::uint64_t external_captures = 0;
  std::uint64_t sendable_captures = 0;
  std::map<CreationObstacle, std::uint64_t> obstacles;
  std::map<abc::Opcode, std::uint64_t> other_method_instructions;
  CreationCounts& operator+=(const CreationCounts& other);
};
void InspectCreations(const corpus::AbcFile& file, const abc::Header& header,
                      const abc::MetadataIndex& index,
                      const abc::MethodLookup& lookup,
                      const abc::MethodInfo& method,
                      const abc::MethodBody& body,
                      const analysis::NormalFlow& graph,
                      CreationCounts* counts);
nlohmann::json SerializeCounts(const CreationCounts& counts);
}  // namespace abcd::tools
#endif  // ABCD_TOOLS_CREATION_INSPECTION_H_
