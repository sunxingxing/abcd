#ifndef ABCD_TOOLS_METHOD_INSPECTION_H_
#define ABCD_TOOLS_METHOD_INSPECTION_H_

#include <array>
#include <cstdint>
#include <iosfwd>
#include <nlohmann/json.hpp>
#include <span>

#include "abcd/abc/metadata_index.h"
#include "abcd/abc/method_attributes.h"
#include "corpus_inspection.h"

namespace abcd::tools {

struct MethodCounts {
  std::array<std::uint64_t, abc::kFunctionKindCount> function_kinds{};
  std::uint64_t sendable_methods = 0;
  std::uint64_t frame_methods = 0;
  std::uint64_t failed_frame_methods = 0;
  std::uint64_t default_call_type_methods = 0;
  std::uint64_t hidden_slots = 0;
  std::uint64_t user_parameters = 0;
  std::uint64_t extra_argument_methods = 0;
  std::uint64_t code_methods = 0;
  std::uint64_t instruction_bytes = 0;
  std::uint64_t decoded_methods = 0;
  std::uint64_t failed_methods = 0;
  std::uint64_t instructions = 0;
  std::uint64_t try_regions = 0;
  std::uint64_t catch_handlers = 0;
  std::uint64_t flow_methods = 0;
  std::uint64_t failed_flow_methods = 0;
  std::uint64_t blocks = 0;
  std::uint64_t edges = 0;
  std::uint64_t resume_edges = 0;
  std::uint64_t dispatch_methods = 0;
  std::uint64_t failed_dispatch_methods = 0;
  std::uint64_t dispatch_spans = 0;
  std::uint64_t protected_blocks = 0;
  std::uint64_t protected_instructions = 0;
  std::uint64_t reference_methods = 0;
  std::uint64_t failed_reference_methods = 0;
  std::uint64_t reference_operands = 0;
  std::uint64_t unique_references = 0;
  std::uint64_t string_literals = 0;
  std::uint64_t literal_arrays = 0;
  std::uint64_t literal_entries = 0;
  std::uint64_t dominator_methods = 0;
  std::uint64_t failed_dominator_methods = 0;
  std::uint64_t reachable_blocks = 0;
  std::uint64_t dominator_edge_visits = 0;
  std::uint64_t dominator_eval_steps = 0;
  std::size_t dominator_peak_bytes = 0;

  MethodCounts& operator+=(const MethodCounts& other);
  [[nodiscard]] std::uint64_t FailureCount() const;
};

const char* StageLabel(InspectionStage stage);
nlohmann::json SerializeCounts(const MethodCounts& counts);

// Caller owns the file and the JSON failure array delimiters. Diagnostics are
// streamed without truncation; only one method body and its analyses are live.
MethodCounts InspectMethods(const corpus::AbcFile& file,
                            const abc::Header& header,
                            const abc::MetadataIndex& index,
                            InspectionStage stage, std::ostream& diagnostics);

}  // namespace abcd::tools

#endif  // ABCD_TOOLS_METHOD_INSPECTION_H_
