#ifndef ABCD_SOURCE_CONTROL_PLAN_H_
#define ABCD_SOURCE_CONTROL_PLAN_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::source {
struct ControlId { std::uint32_t value = 0; auto operator<=>(const ControlId&) const = default; };
struct ControlArms {
  std::optional<ControlId> taken;
  std::optional<ControlId> fallthrough;
};
struct ControlNode {
  analysis::WorkBlockId block;
  std::optional<ControlArms> arms;
  std::optional<ControlId> next;
};
enum class EdgePlacement : std::uint8_t { kNext, kTaken, kFallthrough, kJoin };
struct PlacedEdge {
  analysis::EdgeId edge;
  ControlId owner;
  EdgePlacement placement;
};
struct ControlPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  ControlId entry;
  std::vector<ControlNode> nodes;
  std::vector<PlacedEdge> edges;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ControlError : std::uint8_t {
  kInvalidGraph, kRevision, kCycle, kCrossEntry, kUncoveredBlock, kWorkLimit, kResourceLimit
};
// Requires the verified normal ValueProgram, not old SemanticRegionTree/AST.
// Acyclic single-entry regions only. Early-return arms are allowed; cycles and
// cross-entry regions are typed refusals, never duplicated source subgraphs.
[[nodiscard]] std::variant<ControlPlan, ControlError> BuildControlPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {});
// Independent edge/region ownership check. Does not recompute postdominators.
[[nodiscard]] std::variant<std::uint64_t, ControlError> VerifyControlPlan(
    const analysis::ValueProgram& graph, const ControlPlan& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
