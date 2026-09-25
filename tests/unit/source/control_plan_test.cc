#include "../../../src/source/control_plan.h"
#include "abcd/source/source_tree.h"

#include <array>
#include <cstdlib>
#include <span>
#include <type_traits>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
struct Targets { std::optional<std::uint32_t> taken; std::optional<std::uint32_t> fallthrough; };
analysis::ValueProgram Graph(std::span<const Targets> targets) {
  analysis::ValueProgram graph;
  for (std::size_t i = 0; i < targets.size(); ++i) {
    analysis::WorkBlock block{{0}, 0, {static_cast<std::uint32_t>(graph.edges.size())}, 0};
    const auto edge = [&](std::optional<std::uint32_t> target, analysis::FlowEdgeKind kind) {
      if (target) graph.edges.push_back({{static_cast<std::uint32_t>(i)}, {*target}, kind, block.edge_count++});
    };
    edge(targets[i].taken, analysis::FlowEdgeKind::kTaken);
    edge(targets[i].fallthrough, analysis::FlowEdgeKind::kFallthrough);
    graph.blocks.push_back(block);
  }
  return graph;
}
void Diamond() {
  const std::array<Targets, 4> targets{{{1, 2}, {3, {}}, {{}, 3}, {}}};
  auto graph = Graph(targets);
  auto built = BuildControlPlan(graph);
  auto* plan = std::get_if<ControlPlan>(&built);
  Check(plan && plan->nodes[0].next == ControlId{3});
  Check(plan->nodes[0].arms->taken == ControlId{1});
  Check(plan->nodes[0].arms->fallthrough == ControlId{2});
  Check(plan->edges[2].placement == EdgePlacement::kJoin);
  Check(plan->edges[3].placement == EdgePlacement::kJoin);
  plan->nodes[0].arms->taken = ControlId{2};
  Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, *plan)));
  plan->nodes[0].arms->taken = ControlId{1};
  ++graph.revision.value;
  Check(std::get<ControlError>(VerifyControlPlan(graph, *plan)) == ControlError::kRevision);
}
void Shapes() {
  const std::array<Targets, 3> early_return{{{1, 2}, {}, {}}};
  auto built = BuildControlPlan(Graph(early_return));
  auto* plan = std::get_if<ControlPlan>(&built);
  Check(plan && plan->nodes[0].arms && !plan->nodes[0].next);
  const std::array<Targets, 3> skip{{{2, 1}, {{}, 2}, {}}};
  built = BuildControlPlan(Graph(skip));
  plan = std::get_if<ControlPlan>(&built);
  Check(plan && !plan->nodes[0].arms->taken && plan->nodes[0].arms->fallthrough == ControlId{1});
  Check(plan->nodes[0].next == ControlId{2});
  const std::array<Targets, 2> parallel{{{1, 1}, {}}};
  built = BuildControlPlan(Graph(parallel));
  plan = std::get_if<ControlPlan>(&built);
  Check(plan && plan->edges.size() == 2 && plan->nodes[0].arms);
  Check(!plan->nodes[0].arms->taken && !plan->nodes[0].arms->fallthrough);
  const std::array<Targets, 6> nested{{{1, 4}, {2, 3}, {5, {}}, {5, {}}, {5, {}}, {}}};
  built = BuildControlPlan(Graph(nested));
  plan = std::get_if<ControlPlan>(&built);
  Check(plan && plan->nodes[0].next == ControlId{5});
  Check(plan->nodes[1].arms && !plan->nodes[1].next);
  const std::array<Targets, 4> cross_entry{{{1, 2}, {2, 3}, {3, {}}, {}}};
  Check(std::get<ControlError>(BuildControlPlan(Graph(cross_entry))) == ControlError::kCrossEntry);
  const std::array<Targets, 2> cycle{{{1, {}}, {0, {}}}};
  Check(std::get<ControlError>(BuildControlPlan(Graph(cycle))) == ControlError::kCycle);
  const std::array<Targets, 2> unreachable{{{}, {}}};
  Check(std::get<ControlError>(BuildControlPlan(Graph(unreachable))) == ControlError::kUncoveredBlock);
}
void Scaling() {
  for (const std::uint32_t size : {200U, 201U, 4096U}) {
    std::vector<Targets> targets(size);
    for (std::uint32_t i = 0; i + 1 < size; ++i) targets[i].fallthrough = i + 1;
    const auto graph = Graph(targets);
    const auto built = BuildControlPlan(graph);
    const auto* plan = std::get_if<ControlPlan>(&built);
    Check(plan && plan->nodes.size() == size && plan->edges.size() + 1 == size);
    analysis::SemanticBudget budget;
    budget.work = plan->work - 1;
    Check(std::get<ControlError>(BuildControlPlan(graph, budget)) == ControlError::kWorkLimit);
    budget.work = plan->work;
    Check(std::holds_alternative<ControlPlan>(BuildControlPlan(graph, budget)));
  }
}
}  // namespace

int main() {
  // Executable AST operands are syntax IDs; VM register/value IDs do not convert.
  static_assert(!std::is_convertible_v<analysis::ValueId, ExprId>);
  static_assert(!std::is_convertible_v<analysis::WorkBlockId, ScopeId>);
  FunctionTree tree{};
  tree.names.push_back(u"parameter");
  const auto measured = MeasureTreeStorage(tree);
  Check(std::holds_alternative<TreeStorage>(measured));
  const auto bytes = std::get<TreeStorage>(measured).bytes;
  Check(std::get<TreeStorageError>(MeasureTreeStorage(tree, {bytes - 1, 1})) ==
        TreeStorageError::kResourceLimit);
  Check(std::holds_alternative<TreeStorage>(MeasureTreeStorage(tree, {bytes, 1})));
  Check(std::get<TreeStorageError>(MeasureTreeStorage(tree, {bytes, 0})) ==
        TreeStorageError::kWorkLimit);
  Diamond();
  Shapes();
  Scaling();
}
