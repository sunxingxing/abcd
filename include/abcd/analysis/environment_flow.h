#ifndef ABCD_ANALYSIS_ENVIRONMENT_FLOW_H_
#define ABCD_ANALYSIS_ENVIRONMENT_FLOW_H_

#include "abcd/analysis/environment_index.h"
#include "abcd/analysis/normal_flow.h"

namespace abcd::analysis {
using EnvironmentOperation = std::variant<std::monostate, PushEnvironment,
                                          PopEnvironment, LexicalLoad, LexicalStore>;
struct EnvironmentSite {
  abc::CodeOffset pc;
  EnvironmentOperation operation;
};
struct BoundLexicalSite {
  std::uint32_t instruction = 0;
  LexicalBinding binding;
};
struct EnvironmentAllocation {
  std::uint32_t instruction = 0;
  EnvironmentId environment;
};
struct EnvironmentFlow {
  EnvironmentIndex environments;
  // Indexed by decoded instruction position, not byte offset. Absence means
  // unreachable. Site-based callers use their site index, not ABC positions.
  std::vector<std::optional<EnvironmentRef>> before;
  std::vector<BoundLexicalSite> bindings;
  // Discovery order: parents precede children, not necessarily bytecode order.
  std::vector<EnvironmentAllocation> allocations;
  std::size_t allocated_bytes = 0;
  std::uint64_t instruction_visits = 0;
};
enum class EnvironmentFlowError {
  kExceptionRegions,
  kInvalidGraph,
  kResumeEdge,
  kMalformedLexical,
  kAmbiguousJoin,
  kBinding,
  kResourceLimit,
  kUnsupportedEffect
};
struct EnvironmentFlowFailure {
  EnvironmentFlowError kind;
  abc::CodeOffset offset;
  std::optional<LexicalError> lexical_error = {};
  std::optional<EnvironmentError> environment_error = {};
};

// Validated body and its unchanged BuildNormalFlow graph, under pinned ordinary
// lexical-frame semantics. Computes environment topology only: other operations
// are NOT certified for source emission. Does not read/write acc or slot
// values, prove initialization, validate capture bounds, or resolve target
// function attributes. before[creation] is the current environment, not proof
// that a sendable/class/deprecated creation captures it. Stable-environment
// loops are supported; each allocation site still denotes fresh dynamic
// instances. Resumes/handlers and unequal incoming environment sites require
// later analyses and are explicit errors. O(I+B+E+(P+L)log D) time, bounded
// result plus scratch; graph/body are caller storage. At most four instruction
// visits per input instruction. IDs and capture depths are relative to this
// result's context; no cross-method mixing.
[[nodiscard]] std::variant<EnvironmentFlow, EnvironmentFlowFailure>
AnalyzeEnvironments(const abc::MethodBody& body, const NormalFlow& graph,
                    std::uint64_t frame_size, EnvironmentBudget budget = {});

// Same agreement lattice and ancestry index, over owner-proven effect sites.
// Graph instruction ranges index sites; edges describe topology only. The
// owner must preserve exceptional effect timing and reject unsupported frame
// changes. No synthetic bytecode is required. Results are indexed by sites.
[[nodiscard]] std::variant<EnvironmentFlow, EnvironmentFlowFailure>
AnalyzeEnvironmentSites(std::span<const EnvironmentSite> sites,
                        const NormalFlow& graph, EnvironmentBudget budget = {});
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_ENVIRONMENT_FLOW_H_
