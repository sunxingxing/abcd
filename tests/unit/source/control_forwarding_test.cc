#include "../../../src/source/control_plan.h"

#include <array>
#include <cassert>
#include <iostream>

using namespace abcd;
using namespace abcd::source;
namespace {
using Targets = std::array<std::optional<std::uint32_t>, 2>;
analysis::ValueProgram FromTargets(const std::vector<Targets>& targets) {
  analysis::ValueProgram graph;
  for (std::uint32_t b = 0; b < targets.size(); ++b) {
    analysis::WorkBlock block{{static_cast<std::uint32_t>(graph.ops.size())}, 1,
        {static_cast<std::uint32_t>(graph.edges.size())}, 0};
    for (std::uint8_t e = 0; e < 2; ++e) if (targets[b][e])
      graph.edges.push_back({{b}, {*targets[b][e]}, e ? analysis::FlowEdgeKind::kFallthrough :
          analysis::FlowEdgeKind::kTaken, block.edge_count++});
    analysis::ValueOp op{}; op.block = {b};
    op.operation = block.edge_count ? analysis::ValueOperation{analysis::ValueNop{}} :
        analysis::ValueOperation{analysis::ValueReturn{}};
    graph.ops.push_back(op); graph.blocks.push_back(block);
  }
  return graph;
}
analysis::ValueProgram Graph(std::uint32_t count) {
  std::vector<Targets> targets(8 * count + 1);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto b = 8 * i;
    // A case's optional evaluation has threaded and unthreaded no-op joins.
    // The shared vertex has no definitions or effects; the called arm may.
    targets[b] = {b + 1, b + 7}; targets[b + 1] = {b + 5, b + 2};
    targets[b + 2] = {b + 6, b + 3}; targets[b + 3] = {b + 4, {}};
    targets[b + 4] = {b + 8, {}}; targets[b + 5] = {b + 4, {}};
    targets[b + 6] = {b + 8, {}}; targets[b + 7] = {b + 8, {}};
  }
  return FromTargets(targets);
}
bool Pass(const analysis::ValueProgram& g, analysis::SemanticBudget b = {}) {
  return std::holds_alternative<ControlPlan>(BuildControlPlan(g, b));
}
void Check(std::uint32_t count) {
  const auto graph = Graph(count);
  const auto result = BuildControlPlan(graph);
  if (!std::holds_alternative<ControlPlan>(result)) {
    std::cerr << "count=" << count << " error=" << unsigned(ControlResultError(result)) << '\n';
  }
  assert(std::holds_alternative<ControlPlan>(result));
  const auto& plan = std::get<ControlPlan>(result);
  assert(plan.forwarding.size() == count);
  assert(plan.edges.size() == graph.edges.size());
  assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(graph, plan)));
  // Renumbering and split edge corridors cannot change epsilon semantics.
  std::vector<Targets> renamed(graph.blocks.size()), split(graph.blocks.size());
  const auto rename = [&](std::uint32_t b) {
    return b ? static_cast<std::uint32_t>(graph.blocks.size()) - b : 0U;
  };
  for (const auto& edge : graph.edges) {
    renamed[rename(edge.source.value)][edge.successor] = rename(edge.target.value);
    if (graph.blocks[edge.source.value].edge_count == 1) {
      const auto id = static_cast<std::uint32_t>(split.size());
      split.push_back({edge.target.value, {}}); split[edge.source.value][edge.successor] = id;
    } else split[edge.source.value][edge.successor] = edge.target.value;
  }
  assert(Pass(FromTargets(renamed))); assert(Pass(FromTargets(split)));
  const auto reject = [&](const auto& g, const auto& p) {
    assert(std::holds_alternative<ControlError>(VerifyControlPlan(g, p)));
  };
  auto bad = plan; bad.forwarding[0].incoming.value++; reject(graph, bad);
  bad = plan; bad.nodes[graph.blocks.size()].block = {3}; reject(graph, bad);
  bad = plan; bad.nodes[graph.blocks.size()].next = ControlId{0}; reject(graph, bad);
  bad = plan; bad.edges[0].owner = ControlId{3}; reject(graph, bad);
  auto changed = graph; changed.ops[4].normal = analysis::LanguageEffect::kHeapRead;
  reject(changed, plan); assert(!Pass(changed));
  changed = graph; changed.ops[4].exceptional = analysis::ExceptionalCompletion::kMayThrow;
  reject(changed, plan); assert(!Pass(changed));
  changed = graph; changed.ops[4].result = analysis::ValueId{0};
  reject(changed, plan); assert(!Pass(changed));
  changed = graph; changed.ops[4].use_count = 1;
  reject(changed, plan); assert(!Pass(changed));
  changed = graph;
  changed.values.push_back({analysis::PhiDefinition{{4}}, {}, {}, analysis::ValueLife::kLive});
  reject(changed, plan); assert(!Pass(changed));
  changed = graph;
  changed.edges[0].target = {4}; reject(changed, plan);
  changed = graph;
  analysis::OperandUse phi{}; phi.life = analysis::ValueLife::kLive;
  phi.incoming = analysis::EdgeId{graph.blocks[4].first_edge.value}; changed.uses.push_back(phi);
  reject(changed, plan); assert(!Pass(changed));
  // Exact admitted byte and work boundaries, including the unsuccessful old
  // census and the independent recheck; exhaustion never returns a plan.
  for (bool bytes : {true, false}) {
    std::uint64_t low = 0, high = bytes ? 100000000 : 1000000000;
    assert(Pass(graph, bytes ? analysis::SemanticBudget{high, 1000000000} :
        analysis::SemanticBudget{100000000, high}));
    while (low + 1 < high) {
      const auto mid = low + (high - low) / 2;
      if (Pass(graph, bytes ? analysis::SemanticBudget{mid, 1000000000} :
          analysis::SemanticBudget{100000000, mid})) high = mid;
      else low = mid;
    }
    assert(!Pass(graph, bytes ? analysis::SemanticBudget{high - 1, 1000000000} :
        analysis::SemanticBudget{100000000, high - 1}));
    std::cout << count << (bytes ? " bytes=" : " work=") << high << '\n';
  }
}
}  // namespace
int main() { for (auto count : {1U, 200U, 201U, 400U, 800U}) Check(count); }
