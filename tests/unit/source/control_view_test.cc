#include "../../../src/source/control_view.h"
#include "../../../src/source/control_skeleton.h"
#include "../../../src/source/loop_forest.h"

#include <array>
#include <cstdlib>
#include <type_traits>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool value) { if (!value) std::abort(); }
static_assert(!std::is_convertible_v<ControlNodeId, analysis::WorkBlockId>);
static_assert(!std::is_convertible_v<ControlArcId, analysis::EdgeId>);
static_assert(!std::is_constructible_v<ControlGraphView, analysis::ValueProgram&&>);

void DisjointLoopEntries() {
  // Body island: 0 -> 1 -> 0, with exit 2. Handler island: 3 -> 3.
  // The shared loop kernel must not erase the handler because entry 0 cannot
  // reach it, or invent a semantic edge between the two islands.
  const std::array<ControlVertex, 4> nodes{{{{0}, 2}, {{2}, 1}, {{3}, 0}, {{3}, 1}}};
  const std::array<ControlLink, 4> arcs{{
      {{0}, {1}, analysis::FlowEdgeKind::kTaken, 0},
      {{0}, {2}, analysis::FlowEdgeKind::kFallthrough, 1},
      {{1}, {0}, analysis::FlowEdgeKind::kTaken, 0},
      {{3}, {3}, analysis::FlowEdgeKind::kTaken, 0}}};
  const ControlGraphView view{ControlProjection{{7}, {9}, nodes, arcs}};
  const std::array<ControlNodeId, 2> entries{{{0}, {3}}};
  const auto built = BuildLoopForest(view, entries);
  Check(std::holds_alternative<LoopForest>(built));
  const auto& forest = std::get<LoopForest>(built);
  Check(forest.view == ControlViewKind::kProjection && forest.revision.value == 7 && forest.cfg_revision.value == 9);
  Check(forest.loops.size() == 2 && forest.owners.size() == 4 && forest.backedges.size() == 4);
  Check(forest.owners[0] == forest.owners[1] && !forest.owners[2] && forest.owners[3]);
  Check(forest.owners[0] != forest.owners[3]);
  Check(forest.loops[forest.owners[0]->value].header == ControlNodeId{0});
  Check(forest.loops[forest.owners[3]->value].header == ControlNodeId{3});
  Check(!forest.backedges[0] && !forest.backedges[1] && forest.backedges[2] && forest.backedges[3]);
  ControlQuota quota{{}};
  const auto plan = BuildLoopControl(view, entries, quota);
  Check(plan.view == ControlViewKind::kProjection && plan.loops.size() == 2);
  Check(plan.edges.size() == arcs.size());
  for (const auto& loop : plan.loops) {
    Check(loop.header.value == 0 || loop.header.value == 3);
    Check(loop.body.value == loop.header.value);
    Check(std::get<LoopControl>(plan.nodes[loop.wrapper.value].synthetic).loop.value < 2);
  }
  Check(std::holds_alternative<LoopControl>(plan.nodes[plan.entry.value].synthetic));
  const std::array<ControlNodeId, 1> missing{{{0}}};
  Check(std::get<LoopError>(BuildLoopForest(view, missing)) == LoopError::kUnreachable);
  const std::array<ControlNodeId, 2> duplicate{{{0}, {0}}};
  Check(std::get<LoopError>(BuildLoopForest(view, duplicate)) == LoopError::kInvalidGraph);
  const std::array<ControlNodeId, 1> invalid{{{4}}};
  Check(std::get<LoopError>(BuildLoopForest(view, invalid)) == LoopError::kInvalidGraph);
}
void NormalAdapter() {
  analysis::ValueProgram graph;
  graph.revision = {11}; graph.cfg_revision = {13};
  graph.blocks = {{{0}, 0, {0}, 1}};
  graph.edges = {{{0}, {0}, analysis::FlowEdgeKind::kTaken, 0}};
  const ControlGraphView view{graph};
  Check(view.Kind() == ControlViewKind::kNormal);
  Check(view.Node({0}).first_arc == ControlArcId{0} && view.Arc({0}).target == ControlNodeId{0});
  const auto forest = std::get<LoopForest>(BuildLoopForest(graph));
  Check(forest.view == ControlViewKind::kNormal && forest.loops.size() == 1);
  Check(forest.revision == graph.revision && forest.cfg_revision == graph.cfg_revision);
}
void ProjectedSkeleton() {
  const std::array<ControlVertex, 2> nodes{{{{0}, 1}, {{1}, 0}}};
  const std::array<ControlLink, 1> arcs{{{{0}, {1}, analysis::FlowEdgeKind::kTaken, 0}}};
  const ControlGraphView view{ControlProjection{{7}, {9}, nodes, arcs}};
  const std::array<ControlId, 1> entries{{{0}}};
  ControlQuota quota{{}};
  const auto plan = BuildControlSkeleton(view, entries, quota);
  Check(plan.view == ControlViewKind::kProjection);
  Check(plan.nodes.size() == 2 && plan.nodes[0].next == ControlId{1});
  VerifyControlSkeleton(view, entries, plan, quota);
  analysis::ValueProgram normal;
  normal.revision = {7}; normal.cfg_revision = {9};
  normal.blocks = {{{0}, 0, {0}, 1}, {{0}, 0, {1}, 0}};
  normal.edges = {{{0}, {1}, analysis::FlowEdgeKind::kTaken, 0}};
  // Equal numeric IDs and revisions do not make a projected plan a normal plan.
  Check(std::get<ControlError>(VerifyControlPlan(normal, plan)) == ControlError::kInvalidGraph);
  auto stale = plan;
  stale.revision = {8};
  bool rejected = false;
  try { VerifyControlSkeleton(view, entries, stale, quota); }
  catch (ControlError error) { rejected = error == ControlError::kRevision; }
  Check(rejected);
}
}
int main() { DisjointLoopEntries(); NormalAdapter(); ProjectedSkeleton(); }
