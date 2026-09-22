#ifndef ABCD_TOOLS_SOURCE_INSPECTION_H_
#define ABCD_TOOLS_SOURCE_INSPECTION_H_

#include "abcd/abc/method_lookup.h"

#include <map>
#include <nlohmann/json.hpp>

#include "abcd/abc/frame_layout.h"
#include "abcd/source/acyclic_body.h"

namespace abcd::tools {
enum class SourceProbeError { kNameBudget };
using SourceError =
    std::variant<abc::MetadataError, abc::FrameError, analysis::FlowError,
                 analysis::AccumulatorError, analysis::OrderError,
                 source::BodyEmitError, source::AcyclicError, source::EmitError,
                 SourceProbeError>;
struct SourceObstacle {
  SourceError error;
  std::optional<abc::Opcode> opcode;
  auto operator<=>(const SourceObstacle&) const = default;
};
struct SourceCounts {
  std::uint64_t ready = 0;
  std::uint64_t rejected = 0;
  std::uint64_t bytes = 0;
  std::map<SourceObstacle, std::uint64_t> obstacles;
  SourceCounts& operator+=(const SourceCounts& other);
};

// Exercises the real body writer into a sink with unique register bindings.
// Ready is conditional on caller ABI initialization and recovered wrappers;
// it is NOT proof of record decompilation or historical runtime equivalence.
// Stores only first-obstacle histograms, never source text or per-method logs.
void InspectSource(const corpus::AbcFile& file, const abc::Header& header,
                   const abc::MetadataIndex& index,
                   const abc::MethodLookup& lookup,
                   const abc::MethodInfo& method, const abc::MethodBody& body,
                   SourceCounts* counts);
nlohmann::json SerializeCounts(const SourceCounts& counts);
}  // namespace abcd::tools
#endif
