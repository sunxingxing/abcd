#ifndef ABCD_SOURCE_CONTROL_PLAN_H_
#define ABCD_SOURCE_CONTROL_PLAN_H_

#include "abcd/analysis/value_ir.h"
#include "loop_forest.h"

namespace abcd::source {
struct ControlId { std::uint32_t value = 0; auto operator<=>(const ControlId&) const = default; };
struct ControlArms {
  std::optional<ControlId> taken;
  std::optional<ControlId> fallthrough;
};
enum class LoopTransferKind : std::uint8_t { kBreak, kContinue };
struct LoopControl { LoopId loop; };
using ControlTransferSource = std::variant<analysis::EdgeId, LoopId>;
struct LoopTransfer {
  LoopId loop;
  LoopTransferKind kind;
  // A real CFG edge, or the normal exit of a nested loop wrapper.
  ControlTransferSource source;
};
using ControlSynthetic = std::variant<std::monostate, LoopControl, LoopTransfer>;
struct ControlNode {
  analysis::WorkBlockId block;
  std::optional<ControlArms> arms;
  std::optional<ControlId> next;
  ControlSynthetic synthetic;
};
struct ControlLoop {
  analysis::WorkBlockId header;
  std::optional<LoopId> parent;
  ControlId body;
  ControlId wrapper;
  std::optional<analysis::WorkBlockId> exit;
};
enum class EdgePlacement : std::uint8_t { kNext, kTaken, kFallthrough, kJoin };
struct PlacedEdge {
  analysis::EdgeId edge;
  ControlId owner;
  EdgePlacement placement;
};
struct ControlPlan {
  ControlViewKind view = ControlViewKind::kNormal;
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  ControlId entry;
  std::vector<ControlNode> nodes;
  std::vector<PlacedEdge> edges;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  std::vector<ControlLoop> loops;
};
enum class ControlError : std::uint8_t {
  kInvalidGraph, kRevision, kCycle, kCrossEntry, kUncoveredBlock, kWorkLimit, kResourceLimit,
  kIrreducible, kLoopExits
};
// Requires the verified normal ValueProgram, not old SemanticRegionTree/AST.
// Single-entry regions and natural loops with at most one normal exit target.
// Early function exits are allowed. Irreducible/cross-entry regions and multiple
// loop exit targets are typed refusals, never duplicated source subgraphs.
[[nodiscard]] std::variant<ControlPlan, ControlError> BuildControlPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {});
// Independent original-edge/region ownership check. Does not rerun discovery
// or recompute postdominators. A checked plan is not an executable AST proof.
[[nodiscard]] std::variant<std::uint64_t, ControlError> VerifyControlPlan(
    const analysis::ValueProgram& graph, const ControlPlan& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
