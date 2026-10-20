#include "../../../src/source/control_plan.h"
#include "abcd/source/source_tree.h"

#include <array>
#include <bit>
#include <cstdlib>
#include <span>
#include <type_traits>
#include <source_location>
#include <cstdio>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool condition, std::source_location location = std::source_location::current()) {
  if (!condition) { std::fprintf(stderr, "%s:%u\n", location.file_name(), location.line()); std::abort(); }
}
struct Targets { std::optional<std::uint32_t> taken; std::optional<std::uint32_t> fallthrough; };
analysis::ValueProgram Graph(std::span<const Targets> targets) {
  analysis::ValueProgram graph;
  for (std::size_t i = 0; i < targets.size(); ++i) {
    analysis::WorkBlock block{{static_cast<std::uint32_t>(graph.ops.size())}, 0,
        {static_cast<std::uint32_t>(graph.edges.size())}, 0};
    const auto edge = [&](std::optional<std::uint32_t> target, analysis::FlowEdgeKind kind) {
      if (target) graph.edges.push_back({{static_cast<std::uint32_t>(i)}, {*target}, kind, block.edge_count++});
    };
    edge(targets[i].taken, analysis::FlowEdgeKind::kTaken);
    edge(targets[i].fallthrough, analysis::FlowEdgeKind::kFallthrough);
    if (!block.edge_count) {
      analysis::ValueOp terminal{}; terminal.operation = analysis::ValueReturn{};
      terminal.block = {static_cast<std::uint32_t>(i)};
      graph.ops.push_back(terminal); block.op_count = 1;
    }
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
  // This shared arm is a short-circuit decision, not an illegal body entry.
  built = BuildControlPlan(Graph(cross_entry));
  plan = std::get_if<ControlPlan>(&built);
  Check(plan && plan->nodes[0].decision && plan->nodes[0].decision->evaluate == ControlId{1});
  Check(std::holds_alternative<std::uint64_t>(VerifyControlPlan(Graph(cross_entry), *plan)));
  auto changed = *plan;
  changed.nodes[0].decision->evaluate = {2};
  Check(std::holds_alternative<ControlError>(VerifyControlPlan(Graph(cross_entry), changed)));
  changed = *plan; changed.nodes[0].decision->shared_on_taken = false;
  Check(std::holds_alternative<ControlError>(VerifyControlPlan(Graph(cross_entry), changed)));
  changed = *plan; changed.nodes[0].decision->join = {2};
  Check(std::holds_alternative<ControlError>(VerifyControlPlan(Graph(cross_entry), changed)));
  changed = *plan; changed.nodes[3].decision = *changed.nodes[0].decision;
  Check(std::holds_alternative<ControlError>(VerifyControlPlan(Graph(cross_entry), changed)));
  const std::array<Targets, 6> overlapping{{{1, 2}, {3, 4}, {3, 4}, {5, {}}, {5, {}}, {}}};
  // Empty test vertices have no observable semantics. Their shared epsilon
  // corridors now have distinct verified ports, not duplicated operations.
  // The real switch_optional fixture supplies the source differential.
  auto epsilon = Graph(overlapping);
  built = BuildControlPlan(epsilon); plan = std::get_if<ControlPlan>(&built);
  Check(plan && !plan->forwarding.empty());
  Check(std::holds_alternative<std::uint64_t>(VerifyControlPlan(epsilon, *plan)));
  // The identical control edges with an observable operation cannot use this
  // proof. Keep the old refusal for genuinely shared effectful bodies.
  analysis::ValueOp observed{}; observed.block = {3};
  observed.operation = analysis::ValueNop{}; observed.normal = analysis::LanguageEffect::kHeapRead;
  epsilon.ops.insert(epsilon.ops.begin(), observed);
  epsilon.blocks[3].op_count = 1;
  epsilon.blocks[4].first_op = {1}; epsilon.blocks[5].first_op = {1};
  Check(ControlResultError(BuildControlPlan(epsilon)) == ControlError::kCrossEntry);
  const std::array<Targets, 2> cycle{{{1, {}}, {0, {}}}};
  built = BuildControlPlan(Graph(cycle));
  plan = std::get_if<ControlPlan>(&built);
  Check(plan && plan->loops.size() == 1 && !plan->loops[0].exit);
  const std::array<Targets, 2> unreachable{{{}, {}}};
  Check(std::get<ControlError>(BuildControlPlan(Graph(unreachable))) == ControlError::kUncoveredBlock);
}
void ConditionalDecisionPrefix() {
  // if (a || ((b ? c : d) == e)) yes(); else no();
  // The RHS prefix owns a diamond before its final predicate. Shared yes/no
  // bodies remain single nodes, even when they contain observable operations.
  std::uint64_t previous = 0;
  for (const std::uint32_t count : {1U, 2U, 200U, 201U, 402U}) {
    constexpr std::uint32_t kNodes = 8;
    std::vector<Targets> targets;
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto base = i * kNodes;
      targets.insert(targets.end(), {{base + 6, base + 1}, {base + 2, base + 3},
          {base + 4, {}}, {base + 4, {}}, {base + 6, base + 5},
          {base + 7, {}}, {base + 7, {}},
          {i + 1 == count ? std::nullopt : std::optional{base + kNodes}, {}}});
    }
    const auto graph = Graph(targets);
    const auto built = BuildControlPlan(graph);
    Check(std::holds_alternative<ControlPlan>(built));
    const auto& plan = std::get<ControlPlan>(built);
    Check(std::holds_alternative<std::uint64_t>(VerifyControlPlan(graph, plan)));
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto base = i * kNodes;
      Check(plan.nodes[base].decision && plan.nodes[base].decision->evaluate == ControlId{base + 1} &&
          plan.nodes[base].decision->predicate == ControlId{base + 4} &&
          plan.nodes[base].decision->prefix == DecisionPrefix::kRegion);
      Check(plan.nodes[base + 1].next == ControlId{base + 4});
    }
    if (count == 1) {
      auto wrong = plan; wrong.nodes[0].decision->prefix = DecisionPrefix::kLinear;
      Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, wrong)));
      wrong = plan; wrong.nodes[0].decision->predicate = ControlId{1};
      Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, wrong)));
      wrong = plan; wrong.nodes[1].arms->taken = ControlId{6};
      Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, wrong)));
    }
    if (count == 402) Check(plan.work < 3 * previous);
    previous = plan.work;
  }
}
void Loops() {
  const std::array<Targets, 3> targets{{{1, 2}, {0, {}}, {}}};
  const auto graph = Graph(targets);
  const auto built = BuildControlPlan(graph);
  const auto* plan = std::get_if<ControlPlan>(&built);
  Check(plan && plan->loops.size() == 1 && plan->edges.size() == graph.edges.size());
  Check(plan->loops[0].exit == analysis::WorkBlockId{2});
  Check(std::holds_alternative<std::uint64_t>(VerifyControlPlan(graph, *plan)));
  for (std::size_t i = 0; i < plan->nodes.size(); ++i) {
    if (!std::holds_alternative<LoopTransfer>(plan->nodes[i].synthetic)) continue;
    auto changed = *plan;
    auto& transfer = std::get<LoopTransfer>(changed.nodes[i].synthetic);
    transfer.kind = transfer.kind == LoopTransferKind::kBreak ?
        LoopTransferKind::kContinue : LoopTransferKind::kBreak;
    Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, changed)));
    changed = *plan;
    std::get<LoopTransfer>(changed.nodes[i].synthetic).source = LoopId{0};
    Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, changed)));
  }
  auto changed = *plan;
  changed.loops[0].body = {1};
  Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, changed)));
  changed = *plan;
  changed.loops[0].exit = analysis::WorkBlockId{0};
  Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, changed)));
  // Two backedges to the same header must retain separate incoming identities.
  const std::array<Targets, 1> parallel{{{0, 0}}};
  const auto parallel_built = BuildControlPlan(Graph(parallel));
  const auto* parallel_plan = std::get_if<ControlPlan>(&parallel_built);
  Check(parallel_plan && parallel_plan->nodes[0].arms->taken !=
      parallel_plan->nodes[0].arms->fallthrough);
  // Inner-loop exit becomes an outer continue, without duplicating its body.
  const std::array<Targets, 4> nested{{{1, 3}, {2, 0}, {1, {}}, {}}};
  const auto nested_graph = Graph(nested);
  const auto nested_built = BuildControlPlan(nested_graph);
  const auto* nested_plan = std::get_if<ControlPlan>(&nested_built);
  Check(nested_plan && nested_plan->loops.size() == 2);
  Check(std::holds_alternative<std::uint64_t>(VerifyControlPlan(nested_graph, *nested_plan)));
  // The inner normal exit (4) and an abrupt outer continue (0) coexist.
  const std::array<Targets, 6> outer_continue{{{1, 5}, {2, 4}, {0, 3}, {1, {}}, {0, {}}, {}}};
  const auto outer_graph = Graph(outer_continue);
  const auto outer_result = BuildControlPlan(outer_graph);
  const auto* outer = std::get_if<ControlPlan>(&outer_result);
  Check(outer && outer->loops.size() == 2 &&
      std::holds_alternative<std::uint64_t>(VerifyControlPlan(outer_graph, *outer)));
  bool crossed = false;
  for (std::size_t i = 0; i < outer->nodes.size(); ++i) {
    const auto* transfer = std::get_if<LoopTransfer>(&outer->nodes[i].synthetic);
    if (!transfer || transfer->source != ControlTransferSource{analysis::EdgeId{4}}) continue;
    crossed = true;
    Check(transfer->kind == LoopTransferKind::kContinue && outer->loops[transfer->loop.value].header.value == 0);
    auto wrong = *outer;
    const auto inner = transfer->loop.value == 0 ? LoopId{1} : LoopId{0};
    std::get<LoopTransfer>(wrong.nodes[i].synthetic).loop = inner;
    Check(std::holds_alternative<ControlError>(VerifyControlPlan(outer_graph, wrong)));
  }
  Check(crossed);
  const std::array<Targets, 4> multi_exit{{{1, 2}, {0, 3}, {}, {}}};
  // Separate private return arms are lexical early exits, not two normal
  // continuation ports. Keep the latter rejection independently covered.
  auto exit_graph = Graph(multi_exit);
  const auto exit_built = BuildControlPlan(exit_graph);
  const auto* exit_plan = std::get_if<ControlPlan>(&exit_built);
  Check(exit_plan && std::holds_alternative<std::uint64_t>(VerifyControlPlan(exit_graph, *exit_plan)));
  exit_graph.blocks[2].op_count = 0; exit_graph.blocks[3].op_count = 0;
  Check(std::get<ControlError>(BuildControlPlan(exit_graph)) == ControlError::kLoopExits);
  for (const std::uint32_t depth : {1U, 2U, 200U, 201U, 4096U}) {
    // Each child exits to its parent's latch/header; only the outer loop exits
    // the function region. Compact ownership must not expand ancestor bodies.
    std::vector<Targets> nesting(depth + 1);
    for (std::uint32_t i = 0; i < depth; ++i)
      nesting[i] = {i + 1 < depth ? i + 1 : i, i == 0 ? depth : i - 1};
    const auto deep_graph = Graph(nesting);
    // Deep synthetic graphs need an explicit bounded grant; exhausting the
    // production default is not evidence of an invalid loop representation.
    constexpr std::uint64_t kWorkPerNodeLogBound = 512;
    analysis::SemanticBudget budget;
    budget.work = kWorkPerNodeLogBound * (deep_graph.blocks.size() + deep_graph.edges.size()) *
        std::bit_width(deep_graph.blocks.size());
    const auto deep_built = BuildControlPlan(deep_graph, budget);
    const auto* deep = std::get_if<ControlPlan>(&deep_built);
    Check(deep && deep->loops.size() == depth);
    Check(deep->nodes.size() <= deep_graph.blocks.size() +
        deep_graph.edges.size() + 2 * deep->loops.size());
    Check(deep->work <= budget.work);
    budget.work = deep->work;
    Check(std::holds_alternative<ControlPlan>(BuildControlPlan(deep_graph, budget)));
    --budget.work;
    Check(std::get<ControlError>(BuildControlPlan(deep_graph, budget)) == ControlError::kWorkLimit);
  }
}
void LinearExitCorridors() {
  for (const std::uint32_t length : {1U, 200U, 201U, 402U}) {
    const auto exit = length + 3;
    std::vector<Targets> targets(exit + 1);
    targets[0] = {1, exit}; targets[1] = {2, 3}; targets[2] = {0, {}};
    for (std::uint32_t node = 3; node < exit; ++node) targets[node] = {node + 1, {}};
    const auto graph = Graph(targets);
    const auto built = BuildControlPlan(graph);
    const auto* plan = std::get_if<ControlPlan>(&built);
    Check(plan && plan->loops.size() == 1 && plan->loops[0].exit == analysis::WorkBlockId{exit});
    Check(std::holds_alternative<std::uint64_t>(VerifyControlPlan(graph, *plan)));
  }
  for (const std::uint32_t length : {1U, 200U, 201U, 402U}) {
    const auto inner_exit = length + 4, outer_exit = inner_exit + 1;
    std::vector<Targets> targets(outer_exit + 1);
    targets[0] = {1, outer_exit}; targets[1] = {2, inner_exit};
    targets[2] = {3, 4}; targets[3] = {1, {}};
    for (std::uint32_t node = 4; node < inner_exit; ++node) targets[node] = {node + 1, {}};
    targets[inner_exit] = {0, {}};
    const auto graph = Graph(targets);
    const auto built = BuildControlPlan(graph);
    const auto* plan = std::get_if<ControlPlan>(&built);
    Check(plan && plan->loops.size() == 2);
    Check(std::holds_alternative<std::uint64_t>(VerifyControlPlan(graph, *plan)));
  }
  // An exit arm may branch and reconverge internally. Its join belongs to
  // the loop when all predecessors do, while the header continuation stays out.
  const std::array<Targets, 7> branching{{{1, 6}, {2, 3}, {0, {}}, {4, 5}, {6, {}}, {6, {}}, {}}};
  const auto branching_graph = Graph(branching);
  const auto branched = BuildControlPlan(branching_graph);
  Check(std::holds_alternative<ControlPlan>(branched));
  Check(std::holds_alternative<std::uint64_t>(VerifyControlPlan(branching_graph, std::get<ControlPlan>(branched))));
  const std::array<Targets, 8> shared{{{1, 7}, {2, 3}, {0, {}}, {4, 5}, {6, {}}, {6, {}}, {7, {}}, {}}};
  const auto shared_graph = Graph(shared);
  const auto shared_plan = BuildControlPlan(shared_graph);
  Check(std::holds_alternative<ControlPlan>(shared_plan));
  Check(std::get<ControlPlan>(shared_plan).loops.front().exit == analysis::WorkBlockId{7});
  Check(std::holds_alternative<std::uint64_t>(VerifyControlPlan(shared_graph, std::get<ControlPlan>(shared_plan))));
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
void EarlyExitSequences() {
  // A zero-arc projection port completes its region normally. Treating it as
  // abrupt would run the other arm after it and skip the wrapper continuation.
  const std::array<Targets, 5> normal_port{{{1, 2}, {}, {3, 4}, {}, {}}};
  auto port_graph = Graph(normal_port); port_graph.blocks[1].op_count = 0;
  const auto port_built = BuildControlPlan(port_graph);
  const auto* port_plan = std::get_if<ControlPlan>(&port_built);
  Check(port_plan && !port_plan->nodes[0].next && port_plan->nodes[0].arms->taken == ControlId{1});
  // Nesting each continuing arm makes indentation/output quadratic even
  // though these guards have a single sequential continuation.
  for (const std::uint32_t count : {2U, 200U, 201U, 402U}) {
    for (const bool taken_exit : {false, true}) {
      std::vector<Targets> targets(2 * count + 1);
      for (std::uint32_t i = 0; i < count; ++i)
        targets[2 * i] = taken_exit ? Targets{2 * i + 1, 2 * i + 2} :
                                     Targets{2 * i + 2, 2 * i + 1};
      const auto graph = Graph(targets);
      const auto built = BuildControlPlan(graph);
      const auto* plan = std::get_if<ControlPlan>(&built);
      Check(plan && std::holds_alternative<std::uint64_t>(VerifyControlPlan(graph, *plan)));
      for (std::uint32_t i = 0; i + 1 < count; ++i) {
        const auto& node = plan->nodes[2 * i];
        Check(node.next == ControlId{2 * i + 2});
        Check((taken_exit ? node.arms->taken : node.arms->fallthrough) == ControlId{2 * i + 1});
        Check(!(taken_exit ? node.arms->fallthrough : node.arms->taken));
      }
      auto changed = *plan;
      changed.nodes[0].next = ControlId{1};
      Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, changed)));
    }
  }
}
}  // namespace

int main() {
  ConditionalDecisionPrefix();
  const std::array<Targets, 1> targets{};
  auto phased = Graph(targets);
  const auto normal = BuildControlPlan(phased);
  Check(std::holds_alternative<ControlPlan>(normal));
  phased.phases.push_back({{0}, {0}, analysis::ExceptionPhase::kBefore, {}});
  Check(std::get<ControlError>(BuildControlPlan(phased)) == ControlError::kInvalidGraph);
  Check(std::get<ControlError>(VerifyControlPlan(phased, std::get<ControlPlan>(normal))) ==
      ControlError::kInvalidGraph);
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
  Loops();
  LinearExitCorridors();
  Scaling();
  EarlyExitSequences();
}
