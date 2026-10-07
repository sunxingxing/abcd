#ifndef ABCD_SOURCE_FLOW_DEFINITIONS_H_
#define ABCD_SOURCE_FLOW_DEFINITIONS_H_
#include "flow_liveness.h"
namespace abcd::source {
struct ReachingDefinition {
  FlowPoint point;
  FlowDefinitionId definition;
  auto operator<=>(const ReachingDefinition&) const = default;
};
struct FlowDefinitions {
  // Sorted def-use edges, not a claim that an empty set is an initialized value.
  // External cells and missing entry definitions require separate identity facts.
  std::vector<ReachingDefinition> reads;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
struct FlowReadCoverage {
  // Every entry-reachable read of the binding observes this static definition,
  // or no value is read. Unreachable competing definitions may conservatively
  // prevent coverage. This does not equate dynamic executions of
  // the definition, authorize effect removal, or prove binding lifetime.
  std::vector<std::uint8_t> definitions;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Summarize observed origins from shared all-read liveness without building
// the potentially dense read/definition relation. A live entry value prevents
// coverage, as does any competing live definition (including undefined init).
[[nodiscard]] std::variant<FlowReadCoverage,TreeStructureError> AnalyzeFlowReadCoverage(
    const RecoveryFlow&,const FlowLiveness&,analysis::SemanticBudget = {});
// Forward reaching definitions, pruned by the shared incoming liveness facts.
// Definitions commit only on their graph point; throwing paths cannot acquire
// a later successful write. No syntax, spelling or source layout is inspected.
// The liveness input must come from this exact unchanged graph; original
// CFG/SSA revisions alone do not identify a source projection. The preparation
// owner must discard both products when applying a projection delta.
[[nodiscard]] std::variant<FlowDefinitions,TreeStructureError> AnalyzeFlowDefinitions(
    const RecoveryFlow&,const FlowLiveness&,analysis::SemanticBudget = {});
}
#endif
