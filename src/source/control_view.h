#ifndef ABCD_SOURCE_CONTROL_VIEW_H_
#define ABCD_SOURCE_CONTROL_VIEW_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::source {
struct ControlNodeId { std::uint32_t value = 0; auto operator<=>(const ControlNodeId&) const = default; };
struct ControlArcId { std::uint32_t value = 0; auto operator<=>(const ControlArcId&) const = default; };
enum class ControlViewKind : std::uint8_t { kNormal, kProjection };
// kExitless: a try wrapper with no continuation port (body and handler only
// return/throw/diverge). Layout treats it as kNormal, so planning is unchanged;
// verification knows it cannot fall through to an enclosing continuation.
enum class ControlTermination : std::uint8_t { kNormal, kAbrupt, kExitless };
// Source return remains abrupt. A normalized completion owner can additionally
// require its exact terminal to stay outside a cleanup loop, regardless of PCs.
enum class ControlCompletion : std::uint8_t { kOrdinary, kPreserving };
// Zero arcs do not prove source termination: a projected normal try-exit
// port emits no transfer and continues at its wrapper. Only the owning
// projection may mark an actual return/throw/loop transfer as abrupt.
struct ControlVertex {
  ControlArcId first_arc;
  std::uint8_t arc_count;
  ControlTermination termination = ControlTermination::kNormal;
  ControlCompletion completion = ControlCompletion::kOrdinary;
  // Statement-only unit (try wrapper/exit, protocol close): it can never be
  // part of a short-circuit condition prefix. Layout is otherwise unaffected.
  bool statement = false;
};
// Like the adjacency view, this borrows an already-checked ValueProgram.
inline ControlTermination BlockTermination(const analysis::ValueProgram& graph,
                                           const analysis::WorkBlock& block) {
  if (!block.op_count) return ControlTermination::kNormal;
  const auto& op = graph.ops[block.first_op.value + block.op_count - 1].operation;
  return std::holds_alternative<analysis::ValueReturn>(op) || std::holds_alternative<analysis::ValueThrow>(op) ?
      ControlTermination::kAbrupt : ControlTermination::kNormal;
}
struct ControlLink {
  ControlNodeId source;
  ControlNodeId target;
  analysis::FlowEdgeKind kind;
  std::uint8_t successor;
};
struct ControlProjection {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::span<const ControlVertex> nodes;
  std::span<const ControlLink> arcs;
};
// Borrowed adjacency only. Projection IDs never implicitly become ValueProgram
// IDs; the projection owner retains the mapping back to phases/real edges.
// Both normal and projected loop discovery use this view, without constructing
// a ValueProgram containing synthetic operations or erased exception phases.
class ControlGraphView {
 public:
  explicit ControlGraphView(const analysis::ValueProgram& graph) : source_(&graph) {}
  ControlGraphView(analysis::ValueProgram&&) = delete;
  explicit ControlGraphView(ControlProjection projection) : source_(projection) {}
  ControlViewKind Kind() const {
    return std::holds_alternative<const analysis::ValueProgram*>(source_)
        ? ControlViewKind::kNormal : ControlViewKind::kProjection;
  }
  analysis::IrRevision Revision() const {
    if (const auto* graph = std::get_if<const analysis::ValueProgram*>(&source_)) return (*graph)->revision;
    return std::get<ControlProjection>(source_).revision;
  }
  analysis::CfgRevision CfgRevision() const {
    if (const auto* graph = std::get_if<const analysis::ValueProgram*>(&source_)) return (*graph)->cfg_revision;
    return std::get<ControlProjection>(source_).cfg_revision;
  }
  std::size_t NodeCount() const {
    if (const auto* graph = std::get_if<const analysis::ValueProgram*>(&source_)) return (*graph)->blocks.size();
    return std::get<ControlProjection>(source_).nodes.size();
  }
  std::size_t ArcCount() const {
    if (const auto* graph = std::get_if<const analysis::ValueProgram*>(&source_)) return (*graph)->edges.size();
    return std::get<ControlProjection>(source_).arcs.size();
  }
  ControlVertex Node(ControlNodeId id) const {
    if (const auto* graph = std::get_if<const analysis::ValueProgram*>(&source_)) {
      const auto& block = (*graph)->blocks[id.value];
      return {{block.first_edge.value}, block.edge_count, BlockTermination(**graph, block)};
    }
    return std::get<ControlProjection>(source_).nodes[id.value];
  }
  // Normal input may retain removed origin-only operations after a return.
  // Consumers inspecting executable completion must skip those records. The
  // projection owns its completion fact and has no borrowed operation range.
  std::span<const analysis::ValueOp> Operations(ControlNodeId id) const {
    if (const auto* graph = std::get_if<const analysis::ValueProgram*>(&source_)) {
      const auto& block = (*graph)->blocks[id.value];
      return std::span{(*graph)->ops}.subspan(block.first_op.value, block.op_count);
    }
    return {};
  }
  ControlLink Arc(ControlArcId id) const {
    if (const auto* graph = std::get_if<const analysis::ValueProgram*>(&source_)) {
      const auto& edge = (*graph)->edges[id.value];
      return {{edge.source.value}, {edge.target.value}, edge.kind, edge.successor};
    }
    return std::get<ControlProjection>(source_).arcs[id.value];
  }
 private:
  std::variant<const analysis::ValueProgram*, ControlProjection> source_;
};
}  // namespace abcd::source
#endif
