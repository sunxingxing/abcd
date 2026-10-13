#include "../../../src/source/control_skeleton.h"
#include "../../../src/source/control_failure_json.h"
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
    for (std::uint8_t e = 0; e < 2; ++e) if (targets[i][e]) {
      graph.edges.push_back({{i}, {*targets[i][e]}, e == 0 ? analysis::FlowEdgeKind::kTaken :
          analysis::FlowEdgeKind::kFallthrough, block.edge_count++});
    }
    if (!block.edge_count) {
      analysis::ValueOp op{}; op.block = {i}; op.operation = analysis::ValueReturn{};
      graph.ops.push_back(op); block.op_count = 1;
    }
    graph.blocks.push_back(block);
  }
  return graph;
}
analysis::ValueProgram Terminal(std::size_t count) {
  // Each guard's early arm has an arbitrary number of internal joins. The
  // continuing arm reaches the outer suffix, which must be owned only once.
  std::vector<Targets> nodes(4 * count + 4);
  const auto end = static_cast<std::uint32_t>(nodes.size() - 1);
  nodes[0] = {end, 1}; nodes[1] = {end - 1, 2};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto head = 2 + 4 * i;
    nodes[head] = {head + 1, head + 2};
    nodes[head + 1] = {head + 3, {}};
    nodes[head + 2] = {head + 3, {}};
    if (i + 1 < count) nodes[head + 3] = {head + 4, {}};
  }
  nodes[end - 1] = {end, {}};
  return Graph(nodes);
}
analysis::ValueProgram Split(const analysis::ValueProgram& graph) {
  std::vector<Targets> targets(graph.blocks.size());
  for (const auto& edge : graph.edges) {
    const auto split = static_cast<std::uint32_t>(targets.size());
    targets.push_back({edge.target.value, {}});
    targets[edge.source.value][edge.successor] = split;
  }
  auto result = Graph(targets);
  // Independent/copy operations in split blocks do not define ownership.
  for (std::size_t b = graph.blocks.size(); b < result.blocks.size(); ++b) {
    auto& block = result.blocks[b]; block.first_op = {static_cast<std::uint32_t>(result.ops.size())};
    block.op_count = 1; analysis::ValueOp op{}; op.block = {static_cast<std::uint32_t>(b)};
    op.operation = analysis::ValueCopy{}; result.ops.push_back(op);
  }
  return result;
}
void Positive() {
  for (const auto count : {1U, 2U, 200U, 201U, 400U, 800U}) {
    auto graph = Terminal(count);
    auto built = BuildControlPlan(graph);
    assert(std::holds_alternative<ControlPlan>(built));
    const auto& plan = std::get<ControlPlan>(built);
    assert(plan.nodes[0].next == ControlId{static_cast<std::uint32_t>(graph.blocks.size() - 1)});
    assert(plan.nodes[1].next == ControlId{static_cast<std::uint32_t>(graph.blocks.size() - 2)});
    assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(graph, plan)));
    auto mutant = plan; mutant.nodes[1].next = ControlId{2};
    assert(std::holds_alternative<ControlError>(VerifyControlPlan(graph, mutant)));
    mutant = plan; std::swap(mutant.nodes[1].arms->taken, mutant.nodes[1].arms->fallthrough);
    assert(std::holds_alternative<ControlError>(VerifyControlPlan(graph, mutant)));
    mutant = plan; mutant.nodes[2].next.reset();
    assert(std::holds_alternative<ControlError>(VerifyControlPlan(graph, mutant)));
    mutant = plan; mutant.edges[0].owner = {1};
    assert(std::holds_alternative<ControlError>(VerifyControlPlan(graph, mutant)));
    const auto split = Split(graph);
    const auto split_plan = BuildControlPlan(split);
    assert(std::holds_alternative<ControlPlan>(split_plan));
    assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(split, std::get<ControlPlan>(split_plan))));
    auto quota = analysis::SemanticBudget{}; quota.work = plan.work - 1;
    assert(ControlResultError(BuildControlPlan(graph, quota)) == ControlError::kWorkLimit);
    quota.work = plan.work;
    assert(std::holds_alternative<ControlPlan>(BuildControlPlan(graph, quota)));
    std::cout << count << " nodes=" << graph.blocks.size() << " work=" << plan.work << '\n';
  }
}
void Boundary(std::size_t count) {
  const auto graph = Terminal(count);
  auto budget = analysis::SemanticBudget{};
  std::size_t lo = 0, hi = budget.bytes;
  while (lo < hi) {
    const auto mid = lo + (hi - lo) / 2; budget.bytes = mid;
    const auto result = BuildControlPlan(graph, budget);
    if (std::holds_alternative<ControlPlan>(result)) hi = mid;
    else { assert(ControlResultError(result) == ControlError::kResourceLimit); lo = mid + 1; }
  }
  budget.bytes = lo; assert(std::holds_alternative<ControlPlan>(BuildControlPlan(graph, budget)));
  budget.bytes = lo - 1; assert(ControlResultError(BuildControlPlan(graph, budget)) == ControlError::kResourceLimit);
}
void Negatives() {
  const auto private_graph = Terminal(1);
  const auto private_plan = std::get<ControlPlan>(BuildControlPlan(private_graph));
  // An outside edge into an internal arm destroys the single-entry proof.
  auto external = private_graph; external.edges[0].target = {3};
  assert(std::holds_alternative<ControlError>(VerifyControlPlan(external, private_plan)));
  const auto external_built = BuildControlPlan(external);
  if (const auto* plan = std::get_if<ControlPlan>(&external_built)) {
    assert(plan->nodes[1].next != ControlId{6});
    assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(external, *plan)));
  } else assert(ControlResultError(external_built) == ControlError::kCrossEntry);
  // A join reachable from both arms is shared, not a private return tail.
  const auto shared = Graph({{{7, 1}}, {{6, 2}}, {{3, 4}}, {{5, {}}}, {{5, {}}}, {{7, {}}}, {{7, {}}}, {}});
  assert(std::holds_alternative<ControlError>(VerifyControlPlan(shared, private_plan)));
  const auto shared_plan = std::get<ControlPlan>(BuildControlPlan(shared));
  // A shared-arm decision can own the join at an enclosing node. The
  // obligation is that it must not claim the other arm as a private follow.
  assert(shared_plan.nodes[1].next != ControlId{6});
  assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(shared, shared_plan)));
  // The same topology ending in an unknown/normal completion is not a
  // return. A forged terminal plan must fail against the immutable input.
  const auto returns = Graph({{{1, 2}}, {}, {{3, 4}}, {}, {}});
  const auto forged = std::get<ControlPlan>(BuildControlPlan(returns));
  auto normal_port = returns; normal_port.blocks[1].op_count = 0;
  const auto normal_plan = std::get<ControlPlan>(BuildControlPlan(normal_port));
  assert(!normal_plan.nodes[0].next);
  assert(std::holds_alternative<ControlError>(VerifyControlPlan(normal_port, forged)));
  auto origin_tail = returns;
  auto& tail = origin_tail.blocks[1];
  const auto live_return = origin_tail.ops[tail.first_op.value];
  tail.first_op = {static_cast<std::uint32_t>(origin_tail.ops.size())}; tail.op_count = 2;
  origin_tail.ops.push_back(live_return);
  auto removed = live_return; removed.operation = analysis::ValueNop{};
  removed.life = analysis::ValueLife::kRemoved; origin_tail.ops.push_back(removed);
  assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(origin_tail, forged)));
  auto unknown = returns;
  unknown.ops[unknown.blocks[1].first_op.value].operation = analysis::ValueNop{};
  assert(std::holds_alternative<ControlError>(VerifyControlPlan(unknown, forged)));
  // A backedge cannot be changed into a terminal. Neither the summary nor
  // the verifier may infer that an unknown loop completes.
  auto loop = Graph({{{1, 4}}, {{2, 3}}, {{1, {}}}, {{4, {}}}, {}});
  auto fake = Graph({{{1, 4}}, {{2, 3}}, {}, {{4, {}}}, {}});
  const auto fake_plan = std::get<ControlPlan>(BuildControlPlan(fake));
  assert(std::holds_alternative<ControlError>(VerifyControlPlan(loop, fake_plan)));
  auto loop_plan = std::get<ControlPlan>(BuildControlPlan(loop));
  assert(!loop_plan.loops.empty());
  loop_plan.nodes[loop_plan.loops.front().wrapper.value].synthetic = std::monostate{};
  assert(std::holds_alternative<ControlError>(VerifyControlPlan(loop, loop_plan)));
}
void DoubleTerminal() {
  // Both regions terminate independently. Neither reaches an enclosing
  // continuation, so a virtual follow is already a complete layout choice.
  const auto graph = Graph({{{1, 4}}, {{2, 3}}, {}, {}, {{5, 6}}, {}, {}});
  const auto built = BuildControlPlan(graph);
  assert(std::holds_alternative<ControlPlan>(built));
  const auto& plan = std::get<ControlPlan>(built);
  assert(!plan.nodes[0].next);
  assert(plan.nodes[0].arms->taken == ControlId{1});
  assert(plan.nodes[0].arms->fallthrough == ControlId{4});
  assert(std::holds_alternative<std::uint64_t>(VerifyControlPlan(graph, plan)));
}
void ClassB() {
  const auto graph = Graph({{{6, 1}}, {{4, 2}}, {{4, 3}}, {}, {{5, {}}}, {{6, {}}}, {}});
  const auto built = BuildControlPlan(graph);
  assert(std::holds_alternative<ControlFailure>(built));
  const auto& failure = std::get<ControlFailure>(built);
  assert(failure.kind == ControlError::kCrossEntry);
  assert(failure.cause == ControlOwnershipError::kDuplicate);
  assert(failure.control.node.node == 6);
  assert(failure.control.current.stop && failure.control.current.stop->node == 3);
  assert(failure.control.previous.first.node == 0);
  // No original arc exists for a lexical jump to the chosen join.
  assert(ParseControlJson(ControlJson(failure.control)) == failure.control);
  auto json = ControlJson(failure.control); json["node"]["origin"] = 99;
  assert(!ParseControlJson(json));
  json = ControlJson(failure.control); json["node"]["origin"] = static_cast<unsigned>(ControlOriginKind::kTry);
  json["node"]["pc"] = 8;
  assert(!ParseControlJson(json));
}
}
int main() { DoubleTerminal(); ClassB(); Positive(); Negatives();
  for (auto size : {2U, 200U, 201U, 400U, 800U}) Boundary(size); }
