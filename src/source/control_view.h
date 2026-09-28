#ifndef ABCD_SOURCE_CONTROL_VIEW_H_
#define ABCD_SOURCE_CONTROL_VIEW_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::source {
struct ControlNodeId { std::uint32_t value = 0; auto operator<=>(const ControlNodeId&) const = default; };
struct ControlArcId { std::uint32_t value = 0; auto operator<=>(const ControlArcId&) const = default; };
enum class ControlViewKind : std::uint8_t { kNormal, kProjection };
struct ControlVertex { ControlArcId first_arc; std::uint8_t arc_count; };
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
      return {{block.first_edge.value}, block.edge_count};
    }
    return std::get<ControlProjection>(source_).nodes[id.value];
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
