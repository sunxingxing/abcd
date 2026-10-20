#include "control_forwarding.h"

#include <algorithm>
#include <limits>

namespace abcd::source {
namespace {
using Error = ControlError;
void Require(bool value) { if (!value) throw Error::kInvalidGraph; }
struct ForwardingView {
  std::vector<ControlVertex> nodes;
  std::vector<ControlLink> arcs;
  std::vector<ControlForward> ports;
};
// A shared jump may be threaded by one original predecessor and unthreaded by
// another. Split only the epsilon control, never an expression or declaration.
// A single shared scan accounts for all definitions and edge-qualified reads;
// no per-candidate CFG walks or opcode/PC/template admission (regression:
// control_forwarding_test, switch_optional_cli_test).
ForwardingView Derive(const analysis::ValueProgram& graph, ControlQuota& quota) {
  Require(graph.phases.empty());
  constexpr ControlNodeId root{0};
  auto found = BuildLoopForest(graph, quota.Remaining());
  if (const auto* error = std::get_if<LoopError>(&found)) {
    if (*error == LoopError::kResourceLimit) throw Error::kResourceLimit;
    if (*error == LoopError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kInvalidGraph;
  }
  auto forest = std::get<LoopForest>(std::move(found));
  quota.Work(forest.work); quota.Admit(forest.allocated_bytes, 1);
  std::vector<std::uint8_t> inert;
  std::vector<std::uint32_t> incoming, first;
  quota.Size(inert, graph.blocks.size()); quota.Size(incoming, graph.blocks.size());
  quota.Size(first, graph.blocks.size());
  std::fill(inert.begin(), inert.end(), 1);
  for (std::uint32_t b = 0; b < graph.blocks.size(); ++b) {
    quota.Work(); const auto& block = graph.blocks[b];
    inert[b] = b != root.value && block.edge_count == 1;
    for (std::uint32_t i = 0; i < block.op_count; ++i) {
      quota.Work(); const auto& op = graph.ops[block.first_op.value + i];
      if (op.life != analysis::ValueLife::kLive) continue;
      if (op.result || op.use_count || op.normal != analysis::LanguageEffect::kNone ||
          op.before_throw != analysis::LanguageEffect::kNone ||
          op.exceptional != analysis::ExceptionalCompletion::kImpossible ||
          std::holds_alternative<analysis::ValueReturn>(op.operation) ||
          std::holds_alternative<analysis::ValueThrow>(op.operation)) inert[b] = 0;
    }
  }
  for (const auto& value : graph.values) {
    quota.Work(); if (value.life != analysis::ValueLife::kLive) continue;
    if (const auto* phi = std::get_if<analysis::PhiDefinition>(&value.origin)) inert[phi->block.value] = 0;
    if (const auto* entry = std::get_if<analysis::CatchDefinition>(&value.origin)) inert[entry->block.value] = 0;
  }
  for (const auto& use : graph.uses) {
    quota.Work(); if (use.life != analysis::ValueLife::kLive || !use.incoming) continue;
    if (const auto* edge = std::get_if<analysis::EdgeId>(&*use.incoming)) {
      // An edge-qualified phi is an observable value-transfer boundary, even
      // when its machine jump has no register effects of its own.
      inert[graph.edges[edge->value].source.value] = 0;
    }
  }
  for (const auto& loop : forest.loops) { quota.Work(); inert[loop.header.value] = 0; }
  for (std::uint32_t e = 0; e < graph.edges.size(); ++e) {
    quota.Work(); const auto& edge = graph.edges[e];
    if (!incoming[edge.target.value]++) first[edge.target.value] = e;
    if (forest.owners[edge.source.value] != forest.owners[edge.target.value]) inert[edge.target.value] = 0;
  }
  std::size_t count = 0;
  for (std::uint32_t b = 0; b < graph.blocks.size(); ++b) {
    quota.Work(); const auto& block = graph.blocks[b];
    if (inert[b] && forest.owners[b] != forest.owners[graph.edges[block.first_edge.value].target.value]) inert[b] = 0;
    if (inert[b] && incoming[b] > 1) count += incoming[b] - 1;
  }
  // A split edge can put further inert vertices after a shared join. Bypass
  // the entire epsilon corridor, otherwise a new port merely moves duplicate
  // ownership one block later. Memoized functional-graph closure is linear;
  // a cycle without an activation boundary never supplies a terminal port.
  std::vector<std::uint8_t> state;
  std::vector<std::uint32_t> ends, pending;
  quota.Size(state, graph.blocks.size()); quota.Size(ends, graph.blocks.size());
  quota.Size(pending, graph.blocks.size());
  for (std::uint32_t b = 0; b < graph.blocks.size(); ++b) {
    quota.Work(); if (!inert[b] || state[b] == 2) continue;
    std::size_t size = 0; auto next = b;
    while (inert[next] && !state[next]) {
      quota.Work(); state[next] = 1; pending[size++] = next;
      next = graph.edges[graph.blocks[next].first_edge.value].target.value;
    }
    Require(!inert[next] || state[next] == 2);
    const auto end = inert[next] ? ends[next] : next;
    while (size) { quota.Work(); const auto at = pending[--size]; ends[at] = end; state[at] = 2; }
  }
  Require(count <= std::numeric_limits<std::uint32_t>::max() - graph.blocks.size() &&
      count <= std::numeric_limits<std::uint32_t>::max() - graph.edges.size());
  ForwardingView result;
  quota.Size(result.nodes, graph.blocks.size() + count);
  quota.Size(result.arcs, graph.edges.size() + count);
  quota.Size(result.ports, count);
  const ControlGraphView original(graph);
  for (std::uint32_t b = 0; b < graph.blocks.size(); ++b) {
    quota.Work(); result.nodes[b] = original.Node({b});
  }
  std::uint32_t port = 0;
  for (std::uint32_t e = 0; e < graph.edges.size(); ++e) {
    quota.Work(); auto edge = original.Arc({e}); const auto b = edge.target.value;
    if (inert[b] && incoming[b] > 1 && first[b] != e) {
      const auto id = static_cast<std::uint32_t>(graph.blocks.size()) + port;
      const auto arc = static_cast<std::uint32_t>(graph.edges.size()) + port;
      const auto successor = original.Arc({graph.blocks[b].first_edge.value});
      result.ports[port++] = {{b}, {e}};
      result.nodes[id] = {{arc}, 1};
      result.arcs[arc] = {{id}, {ends[b]}, successor.kind, 0};
      edge.target = {id};
    }
    result.arcs[e] = edge;
  }
  Require(port == count);
  return result;
}
ControlGraphView View(const analysis::ValueProgram& graph, const ForwardingView& view) {
  return ControlGraphView(ControlProjection{graph.revision, graph.cfg_revision, view.nodes, view.arcs});
}
}  // namespace

ControlPlan BuildForwardingControl(const analysis::ValueProgram& graph,
    const ControlFailure& failure, ControlQuota& quota) {
  if (failure.kind != Error::kCrossEntry ||
      failure.cause != decltype(failure.cause){ControlOwnershipError::kDuplicate}) throw failure;
  auto view = Derive(graph, quota);
  const auto witness = failure.control.node;
  bool found = false;
  for (const auto& port : view.ports) { quota.Work(); found = found || port.block.value == witness.source; }
  if (witness.origin != ControlOriginKind::kBlock || !found) throw failure;
  constexpr ControlId entry{0};
  auto plan = [&] {
    try { return BuildControlSkeleton(View(graph, view), {&entry, 1}, quota); }
    catch (Error error) {
      if (error != Error::kCycle) throw;
      constexpr ControlNodeId root{0};
      return BuildLoopControl(View(graph, view), {&root, 1}, quota);
    }
  }();
  for (std::size_t i = 0; i < view.ports.size(); ++i) {
    quota.Work(); auto& node = plan.nodes[graph.blocks.size() + i];
    Require(std::holds_alternative<std::monostate>(node.synthetic));
    node.synthetic = view.ports[i]; node.block = view.ports[i].block;
  }
  // The normal plan exports exactly the real EdgeId range. Epsilon links
  // exist only in the explicitly borrowed ControlProjection used by checking.
  plan.edges.resize(graph.edges.size());
  plan.forwarding = std::move(view.ports); plan.view = ControlViewKind::kNormal;
  plan.allocated_bytes += plan.forwarding.capacity() * sizeof(ControlForward);
  return plan;
}

void VerifyForwardingControl(const analysis::ValueProgram& graph,
    const ControlPlan& plan, ControlQuota& quota) {
  // Recompute effect/SSA/activation evidence from fresh immutable inputs, not
  // builder flags. A shadow restores only the borrowed projection identity;
  // the independent region verifier still proves every arc and unique owner.
  auto view = Derive(graph, quota);
  Require(!view.ports.empty() && plan.forwarding == view.ports &&
      plan.nodes.size() >= view.nodes.size() && plan.edges.size() == graph.edges.size());
  ControlPlan shadow;
  quota.Size(shadow.nodes, plan.nodes.size()); quota.Size(shadow.edges, view.arcs.size());
  quota.Size(shadow.loops, plan.loops.size());
  shadow.view = ControlViewKind::kProjection; shadow.revision = plan.revision;
  shadow.cfg_revision = plan.cfg_revision; shadow.entry = plan.entry;
  std::copy(plan.nodes.begin(), plan.nodes.end(), shadow.nodes.begin());
  std::copy(plan.edges.begin(), plan.edges.end(), shadow.edges.begin());
  std::copy(plan.loops.begin(), plan.loops.end(), shadow.loops.begin());
  for (std::size_t i = 0; i < view.ports.size(); ++i) {
    quota.Work(); const auto id = graph.blocks.size() + i; auto& node = shadow.nodes[id];
    Require(node.block == view.ports[i].block &&
        std::get_if<ControlForward>(&node.synthetic) && std::get<ControlForward>(node.synthetic) == view.ports[i]);
    node.block = {static_cast<std::uint32_t>(id)}; node.synthetic = std::monostate{};
    const auto arc = static_cast<std::uint32_t>(graph.edges.size() + i);
    // A one-successor epsilon port either continues lexically or reaches the
    // enclosing stop. VerifyControlRegions checks that stop against its real
    // projected successor; neither option can change the target/completion.
    shadow.edges[arc] = {{arc}, {static_cast<std::uint32_t>(id)},
        node.next ? EdgePlacement::kNext : EdgePlacement::kJoin};
  }
  for (std::size_t i = 0; i < graph.blocks.size(); ++i) {
    quota.Work(); Require(!std::holds_alternative<ControlForward>(shadow.nodes[i].synthetic));
  }
  constexpr ControlNodeId root{0};
  VerifyControlRegions(View(graph, view), {&root, 1}, shadow, quota);
}
}  // namespace abcd::source
