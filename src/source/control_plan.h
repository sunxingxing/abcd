#ifndef ABCD_SOURCE_CONTROL_PLAN_H_
#define ABCD_SOURCE_CONTROL_PLAN_H_

#include "abcd/analysis/value_ir.h"
#include "loop_forest.h"
#include <memory>
#include "abcd/source/control_failure.h"

#include "abcd/source/source_ids.h"

namespace abcd::source {
struct ControlArms {
  std::optional<ControlId> taken;
  std::optional<ControlId> fallthrough;
};
enum class LoopTransferKind : std::uint8_t { kBreak, kContinue };
struct LoopControl { LoopId loop; bool operator==(const LoopControl&) const = default; };
using ControlTransferSource = std::variant<analysis::EdgeId, LoopId>;
struct LoopTransfer {
  LoopId loop;
  LoopTransferKind kind;
  // A real CFG edge, or the normal exit of a nested loop wrapper.
  ControlTransferSource source;
  bool operator==(const LoopTransfer&) const = default;
};
// No operation/definition is emitted at this epsilon port. Its source edge
// still enters the original inert block; verification proves the bypass has
// no input, output, exceptional or lexical-activation observation.
struct ControlForward {
  analysis::WorkBlockId block;
  analysis::EdgeId incoming;
  auto operator<=>(const ControlForward&) const = default;
};
struct IteratorScopeControl { IteratorProtocolId protocol; auto operator<=>(const IteratorScopeControl&) const = default; };
struct SwitchPort { std::uint32_t port; auto operator<=>(const SwitchPort&) const = default; };
using ControlSynthetic = std::variant<std::monostate, LoopControl, LoopTransfer, ControlForward, SwitchPort, IteratorScopeControl>;
struct SwitchRegions;
// An original single-entry evaluation path and its ending condition, entered
// only when the current test misses the shared arm. Original arcs remain in
// PlacedEdge; a final private fallback stays in the last condition's other arm.
// This is control ownership, not permission to inline its effects/phi copies.
enum class DecisionPrefix : std::uint8_t { kLinear, kRegion };
struct ControlDecision {
  ControlId evaluate;
  bool evaluate_on_taken;
  bool shared_on_taken;
  ControlId join;
  // Absent for an adjacent predicate; otherwise the end of a single-entry
  // evaluation prefix. A region prefix can contain nested arms, with its
  // top-level continuation explicitly pointing to this predicate boundary.
  std::optional<ControlId> predicate;
  DecisionPrefix prefix = DecisionPrefix::kLinear;
};
struct ControlNode {
  analysis::WorkBlockId block;
  std::optional<ControlArms> arms;
  std::optional<ControlId> next;
  ControlSynthetic synthetic;
  std::optional<ControlDecision> decision;
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
  std::vector<ControlForward> forwarding;
  std::shared_ptr<const SwitchRegions> switches;
};
enum class ControlError : std::uint8_t {
  kInvalidGraph, kRevision, kCycle, kCrossEntry, kUncoveredBlock, kWorkLimit, kResourceLimit,
  kIrreducible, kLoopExits
};
struct ControlFailure {
  ControlError kind;
  std::variant<ControlOwnershipError, DecisionContinuationError> cause;
  ControlOwnershipEvidence control;
};
using ControlResult = std::variant<ControlPlan, ControlError, ControlFailure>;
inline ControlError ControlResultError(const ControlResult& result) {
  if (const auto* error = std::get_if<ControlError>(&result)) return *error;
  return std::get<ControlFailure>(result).kind;
}
// Requires the verified normal ValueProgram, not old SemanticRegionTree/AST.
// Single-entry regions and natural loops with at most one normal exit target.
// Early function exits and single-entry shared-arm decision chains are allowed.
// Irreducible/genuine cross-entry regions and multiple
// loop exit targets are typed refusals, never duplicated source subgraphs.
[[nodiscard]] ControlResult BuildControlPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {});
// Independent original-edge/region ownership check. Does not rerun discovery
// or recompute postdominators. A checked plan is not an executable AST proof.
[[nodiscard]] std::variant<std::uint64_t, ControlError> VerifyControlPlan(
    const analysis::ValueProgram& graph, const ControlPlan& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
