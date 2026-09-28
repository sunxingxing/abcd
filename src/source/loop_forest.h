#ifndef ABCD_SOURCE_LOOP_FOREST_H_
#define ABCD_SOURCE_LOOP_FOREST_H_

#include "control_view.h"

namespace abcd::source {
struct LoopId { std::uint32_t value; auto operator<=>(const LoopId&) const = default; };
struct NaturalLoop {
  ControlNodeId header;
  std::optional<LoopId> parent;
};
struct LoopForest {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  ControlViewKind view = ControlViewKind::kNormal;
  std::vector<NaturalLoop> loops;
  // Innermost owner only. Never replicate blocks in every enclosing loop.
  std::vector<std::optional<LoopId>> owners;
  // Indexed by this view's ControlArcId; parallel alternatives stay distinct.
  std::vector<std::optional<LoopId>> backedges;
  // Topological order with precisely the dominance backedges removed.
  std::vector<ControlNodeId> forward_order;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class LoopError : std::uint8_t {
  kInvalidGraph, kUnreachable, kIrreducible, kResourceLimit, kWorkLimit
};
// Normal-CFG candidate discovery, not executable-source certification. Uses
// existing Lengauer-Tarjan dominance and compressed reverse loop discovery;
// final region/edge/AST checks must still prove each break/continue placement.
// No dependency on block numbering, PCs, or bytecode fallthrough adjacency.
[[nodiscard]] std::variant<LoopForest, LoopError> BuildLoopForest(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {});
// Multiple roots represent independently entered projected body/handler
// regions. Synthetic dominance roots never appear in the returned forest.
[[nodiscard]] std::variant<LoopForest, LoopError> BuildLoopForest(
    ControlGraphView graph, std::span<const ControlNodeId> entries,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
