#include "../../../src/source/control_skeleton.h"

#include <array>
#include <cassert>
#include <iostream>

using namespace abcd;
using namespace abcd::source;
namespace {
using Targets = std::array<std::optional<std::uint32_t>, 2>;
analysis::ValueProgram Graph(const std::vector<Targets>& targets) {
  analysis::ValueProgram graph;
  for (std::uint32_t i = 0; i < targets.size(); ++i) {
    analysis::WorkBlock block{{static_cast<std::uint32_t>(graph.ops.size())}, 0,
        {static_cast<std::uint32_t>(graph.edges.size())}, 0};
    for (std::uint8_t e = 0; e < 2; ++e) if (targets[i][e])
      graph.edges.push_back({{i}, {*targets[i][e]}, e ? analysis::FlowEdgeKind::kFallthrough :
          analysis::FlowEdgeKind::kTaken, block.edge_count++});
    // These graph stubs retain one operation per vertex. The real fixture
    // separately checks that effectful shared tails are never duplicated.
    analysis::ValueOp op{}; op.block = {i};
    op.operation = block.edge_count ? analysis::ValueOperation{analysis::ValueNop{}} :
        analysis::ValueOperation{analysis::ValueReturn{}};
    graph.ops.push_back(op); block.op_count = 1; graph.blocks.push_back(block);
  }
  return graph;
}
analysis::ValueProgram Shared(std::uint32_t count) {
  // Two nested logical guards, an early completion shared by both predicates,
  // and a continuing suffix also entered from outside the inner decision.
  std::vector<Targets> targets(9 + 4 * count);
  targets[0] = {8, 1}; targets[1] = {8, 2}; targets[2] = {5, 3};
  targets[3] = {5, 4}; targets[5] = {7, 6}; targets[6] = {8, 7};
  targets[7] = {9, {}};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto b = 9 + 4 * i;
    targets[b] = {b + 1, b + 2}; targets[b + 1] = {b + 3, {}};
    targets[b + 2] = {b + 3, {}};
    if (i + 1 < count) targets[b + 3] = {b + 4, {}};
  }
  return Graph(targets);
}
analysis::ValueProgram Split(const analysis::ValueProgram& graph) {
  std::vector<Targets> targets(graph.blocks.size());
  for (const auto& edge : graph.edges) {
    if (graph.blocks[edge.source.value].edge_count == 1) {
      const auto id = static_cast<std::uint32_t>(targets.size());
      targets.push_back({edge.target.value, {}}); targets[edge.source.value][edge.successor] = id;
    } else targets[edge.source.value][edge.successor] = edge.target.value;
  }
  return Graph(targets);
}
void Rejected(const analysis::ValueProgram& graph, const ControlPlan& plan) {
  assert(std::holds_alternative<ControlError>(VerifyControlPlan(graph, plan)));
}
void Check(std::uint32_t count) {
  const auto graph = Shared(count);
  const auto result = BuildControlPlan(graph);
  if (!std::holds_alternative<ControlPlan>(result)) {
    std::cerr << "count=" << count << " error=" << static_cast<unsigned>(ControlResultError(result));
    if (const auto* failure = std::get_if<ControlFailure>(&result))
      std::cerr << " node=" << failure->control.node.node << " current=" << failure->control.current.first.node;
    std::cerr << '\n';
  }
  assert(std::holds_alternative<ControlPlan>(result));
  const auto& plan = std::get<ControlPlan>(result);
  assert(plan.nodes.size() == graph.blocks.size() && plan.edges.size() == graph.edges.size());
  assert(plan.nodes[0].next == ControlId{8});
  assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(graph, plan)));
  auto bad = plan; bad.nodes[5].next = ControlId{7}; Rejected(graph, bad);
  bad = plan; bad.nodes[6].arms->taken = ControlId{7}; Rejected(graph, bad);
  bad = plan; bad.edges[0].owner = ControlId{1}; Rejected(graph, bad);
  bad = plan; bad.nodes[7].next.reset(); Rejected(graph, bad);
  auto changed = graph; changed.edges[0].target = {9}; Rejected(changed, plan);
  changed = graph; changed.ops.back().operation = analysis::ValueNop{}; Rejected(changed, plan);
  const auto split = Split(graph);
  const auto split_result = BuildControlPlan(split);
  assert(std::holds_alternative<ControlPlan>(split_result));
  assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(split, std::get<ControlPlan>(split_result))));
  std::vector<Targets> reordered(graph.blocks.size());
  const auto renamed = [&](std::uint32_t id) {
    return id == 0 ? 0U : static_cast<std::uint32_t>(graph.blocks.size()) - id;
  };
  for (const auto& edge : graph.edges)
    reordered[renamed(edge.source.value)][edge.successor] = renamed(edge.target.value);
  const auto permuted = Graph(reordered);
  const auto permuted_result = BuildControlPlan(permuted);
  assert(std::holds_alternative<ControlPlan>(permuted_result));
  assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(permuted, std::get<ControlPlan>(permuted_result))));
  // A projection carries completion facts separately; an unknown normal port
  // must not gain source-return semantics from the candidate layout.
  const ControlGraphView normal(graph);
  std::vector<ControlVertex> nodes; std::vector<ControlLink> edges;
  for (std::uint32_t n = 0; n < normal.NodeCount(); ++n) nodes.push_back(normal.Node({n}));
  for (std::uint32_t e = 0; e < normal.ArcCount(); ++e) edges.push_back(normal.Arc({e}));
  const ControlGraphView view(ControlProjection{graph.revision, graph.cfg_revision, nodes, edges});
  ControlQuota quota({}); const ControlId root{0};
  const auto projected = BuildControlSkeleton(view, {&root, 1}, quota);
  VerifyControlSkeleton(view, {&root, 1}, projected, quota);
  nodes.back().termination = ControlTermination::kNormal;
  bool refused = false;
  try { VerifyControlSkeleton(view, {&root, 1}, projected, quota); }
  catch (ControlError) { refused = true; }
  assert(refused);
  auto budget = analysis::SemanticBudget{}; budget.work = plan.work;
  assert(std::holds_alternative<ControlPlan>(BuildControlPlan(graph, budget)));
  --budget.work; assert(ControlResultError(BuildControlPlan(graph, budget)) == ControlError::kWorkLimit);
  std::size_t low = 0, high = analysis::SemanticBudget{}.bytes;
  while (low < high) {
    const auto mid = low + (high - low) / 2; budget = {}; budget.bytes = mid;
    const auto attempt = BuildControlPlan(graph, budget);
    if (std::holds_alternative<ControlPlan>(attempt)) high = mid;
    else { assert(ControlResultError(attempt) == ControlError::kResourceLimit); low = mid + 1; }
  }
  budget = {}; budget.bytes = low;
  assert(std::holds_alternative<ControlPlan>(BuildControlPlan(graph, budget)));
  --budget.bytes; assert(ControlResultError(BuildControlPlan(graph, budget)) == ControlError::kResourceLimit);
  std::cout << count << " nodes=" << graph.blocks.size() << " work=" << plan.work << " bytes=" << low << '\n';
}
}  // namespace
int main() { for (const auto count : {1U, 200U, 201U, 400U, 800U}) Check(count); }
