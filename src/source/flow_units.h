#ifndef ABCD_SOURCE_FLOW_UNITS_H_
#define ABCD_SOURCE_FLOW_UNITS_H_
#include "recovery_flow.h"
namespace abcd::source {
struct FlowUnitQuery {
  ScopeId original;
  SymbolId current;
  // A checked raw allocation's success marker. Without one, compare source
  // scope resets. Neither alternative denotes declaration initialization.
  std::optional<FlowPoint> reset;
};
enum class FlowUnitRelation : std::uint8_t {kEquivalent,kDifferent,kUnmodeled};
struct FlowUnitRelations {
  std::vector<FlowUnitRelation> queries;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Compare dynamic-unit partitions at observable accesses, not allocation-site
// IDs. A path between consecutive observations must reset both units or neither.
// Closed units use reference/definition observations; external units also
// require the shared complete capture census. Identical
// lifetimes do not need that restriction. One batched finite-state worklist,
// One shared SCC/dominator index, plus sparse observation/fact-edge work.
// Cost includes visited fact-edge incidences, not just graph size; each
// (point, lifetime pair) has at most four reset states, admitted by the budget.
// See algorithms.md; dense binding demands are measured rather than discarded.
[[nodiscard]] std::variant<FlowUnitRelations,TreeStructureError> AnalyzeFlowUnits(
    const RecoveryFlow&,std::span<const FlowUnitQuery>,analysis::SemanticBudget={});
}
#endif
