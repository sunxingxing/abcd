#include "../../../src/source/loop_forest.h"
#include "../../../src/source/control_plan.h"

#include <array>
#include <bit>
#include <cstdlib>
#include <span>
#include <utility>

namespace {
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
struct Targets { std::optional<std::uint32_t> first; std::optional<std::uint32_t> second; };
ValueProgram Graph(std::span<const Targets> targets) {
  ValueProgram graph;
  for (std::uint32_t i = 0; i < targets.size(); ++i) {
    WorkBlock block{{0}, 0, {static_cast<std::uint32_t>(graph.edges.size())}, 0};
    if (targets[i].first) graph.edges.push_back({{i}, {*targets[i].first}, FlowEdgeKind::kTaken, block.edge_count++});
    if (targets[i].second) graph.edges.push_back({{i}, {*targets[i].second}, FlowEdgeKind::kFallthrough, block.edge_count++});
    graph.blocks.push_back(block);
  }
  return graph;
}
LoopForest Build(const ValueProgram& graph) {
  auto result = BuildLoopForest(graph);
  Check(std::holds_alternative<LoopForest>(result));
  auto forest = std::get<LoopForest>(std::move(result));
  Check(forest.owners.size() == graph.blocks.size() && forest.backedges.size() == graph.edges.size());
  std::vector<std::size_t> positions(graph.blocks.size());
  for (std::size_t i = 0; i < forest.forward_order.size(); ++i) positions[forest.forward_order[i].value] = i;
  for (std::size_t i = 0; i < graph.edges.size(); ++i) {
    const auto& edge = graph.edges[i];
    if (!forest.backedges[i]) Check(positions[edge.source.value] < positions[edge.target.value]);
    else Check(forest.loops[forest.backedges[i]->value].header == edge.target);
  }
  return forest;
}
void Shapes() {
  const std::array<Targets, 2> self{{{0, 1}, {}}};
  auto forest = Build(Graph(self));
  Check(forest.loops.size() == 1 && forest.loops[0].header == WorkBlockId{0});
  Check(forest.owners[0] == LoopId{0} && !forest.owners[1]);
  Check(forest.backedges[0] == LoopId{0} && !forest.backedges[1]);
  const std::array<Targets, 4> nested{{{1, 3}, {2, 0}, {1, {}}, {}}};
  forest = Build(Graph(nested));
  Check(forest.loops.size() == 2);
  const auto inner = *forest.owners[1];
  const auto outer = *forest.owners[0];
  Check(forest.loops[inner.value].parent == outer && !forest.loops[outer.value].parent);
  Check(forest.owners[2] == inner && !forest.owners[3]);
  // Physical block order is irrelevant; header 2 owns the earlier-numbered 1.
  const std::array<Targets, 4> reordered{{{2, {}}, {2, {}}, {1, 3}, {}}};
  forest = Build(Graph(reordered));
  Check(forest.loops.size() == 1 && forest.loops[0].header == WorkBlockId{2});
  Check(forest.owners[1] == forest.owners[2] && !forest.owners[0]);
  const std::array<Targets, 3> multiple{{{1, 2}, {0, 0}, {}}};
  forest = Build(Graph(multiple));
  Check(forest.loops.size() == 1 && forest.backedges[2] == forest.backedges[3]);
  Check(forest.backedges[2].has_value());
  const std::array<Targets, 3> irreducible{{{1, 2}, {2, {}}, {1, {}}}};
  Check(std::get<LoopError>(BuildLoopForest(Graph(irreducible))) == LoopError::kIrreducible);
  Check(std::get<ControlError>(BuildControlPlan(Graph(irreducible))) == ControlError::kIrreducible);
  const std::array<Targets, 2> unreachable{{{0, {}}, {}}};
  Check(std::get<LoopError>(BuildLoopForest(Graph(unreachable))) == LoopError::kUnreachable);
  auto invalid = Graph(self);
  invalid.edges[0].target = {2};
  Check(std::get<LoopError>(BuildLoopForest(invalid)) == LoopError::kInvalidGraph);
  invalid = Graph(self); invalid.edges[0].kind = FlowEdgeKind::kResume;
  Check(std::get<LoopError>(BuildLoopForest(invalid)) == LoopError::kInvalidGraph);
  invalid = Graph(self); invalid.blocks[0].first_edge = {1};
  Check(std::get<LoopError>(BuildLoopForest(invalid)) == LoopError::kInvalidGraph);
}
void DeepNesting() {
  for (const std::uint32_t depth : {1U, 2U, 200U, 201U, 4096U}) {
    // Every header enters the next inner loop or returns to its parent's
    // header; the outermost alternative exits. No inclusive membership lists.
    std::vector<Targets> targets(depth + 1);
    for (std::uint32_t i = 0; i < depth; ++i)
      targets[i] = {i + 1 == depth ? i : i + 1, i == 0 ? depth : i - 1};
    const auto graph = Graph(targets);
    const auto forest = Build(graph);
    Check(forest.loops.size() == depth && !forest.owners.back());
    for (std::uint32_t i = 0; i < depth; ++i) {
      Check(forest.owners[i].has_value());
      const auto& loop = forest.loops[forest.owners[i]->value];
      Check(loop.header == WorkBlockId{i});
      Check(loop.parent == (i == 0 ? std::optional<LoopId>{} : forest.owners[i - 1]));
    }
    constexpr std::uint64_t kWorkPerItemLogBound = 256;
    Check(forest.work <= kWorkPerItemLogBound * (graph.blocks.size() + graph.edges.size()) *
        std::bit_width(graph.blocks.size()));
    SemanticBudget budget;
    budget.work = forest.work;
    Check(std::holds_alternative<LoopForest>(BuildLoopForest(graph, budget)));
    --budget.work;
    Check(std::get<LoopError>(BuildLoopForest(graph, budget)) == LoopError::kWorkLimit);
    budget = {}; budget.bytes = 0;
    Check(std::get<LoopError>(BuildLoopForest(graph, budget)) == LoopError::kResourceLimit);
  }
}
// Deliberately slow independent oracle, restricted to three blocks. It derives
// dominance by removing vertices and natural membership by predecessor closure,
// not by union-find or dominator-tree traversal used in production.
void SmallGraphs() {
  constexpr std::uint32_t kBlocks = 3;
  constexpr std::uint32_t kChoices = kBlocks + 1;  // Last choice means no edge.
  constexpr std::uint32_t kAllBlocks = (1U << kBlocks) - 1;
  constexpr std::uint32_t kGraphCount = kChoices * kChoices * kChoices * kChoices * kChoices * kChoices;
  for (std::uint32_t encoded = 0; encoded < kGraphCount; ++encoded) {
    std::array<Targets, kBlocks> targets;
    auto digits = encoded;
    for (auto& target : targets) {
      const auto first = digits % kChoices; digits /= kChoices;
      const auto second = digits % kChoices; digits /= kChoices;
      if (first != kBlocks) target.first = first;
      if (second != kBlocks) target.second = second;
    }
    const auto graph = Graph(targets);
    const auto reachable = [&](std::optional<std::uint32_t> removed) {
      std::uint32_t seen = removed == 0 ? 0 : 1;
      bool changed = true;
      while (changed) {
        changed = false;
        for (const auto& edge : graph.edges) {
          if (removed == edge.target.value || !(seen & (1U << edge.source.value))) continue;
          const auto bit = 1U << edge.target.value;
          changed |= !(seen & bit); seen |= bit;
        }
      }
      return seen;
    };
    const auto result = BuildLoopForest(graph);
    if (reachable({}) != kAllBlocks) {
      Check(std::get<LoopError>(result) == LoopError::kUnreachable);
      continue;
    }
    std::array<std::uint32_t, kBlocks> dominated{};
    std::array<std::uint32_t, kBlocks> members{};
    for (std::uint32_t h = 0; h < kBlocks; ++h) dominated[h] = kAllBlocks & ~reachable(h);
    std::vector<bool> backs(graph.edges.size());
    for (std::size_t e = 0; e < graph.edges.size(); ++e) {
      const auto& edge = graph.edges[e];
      backs[e] = dominated[edge.target.value] & (1U << edge.source.value);
      if (backs[e]) members[edge.target.value] |= (1U << edge.target.value) | (1U << edge.source.value);
    }
    for (std::uint32_t h = 0; h < kBlocks; ++h) {
      bool changed = members[h] != 0;
      while (changed) {
        changed = false;
        for (const auto& edge : graph.edges) {
          if (edge.target.value == h || !(members[h] & (1U << edge.target.value))) continue;
          const auto bit = 1U << edge.source.value;
          changed |= !(members[h] & bit); members[h] |= bit;
        }
      }
    }
    std::uint32_t removed = 0;
    bool progress = true;
    while (progress) {
      progress = false;
      for (std::uint32_t b = 0; b < kBlocks; ++b) {
        if (removed & (1U << b)) continue;
        bool predecessor = false;
        for (std::size_t e = 0; e < graph.edges.size(); ++e)
          predecessor |= !backs[e] && graph.edges[e].target.value == b &&
              !(removed & (1U << graph.edges[e].source.value));
        if (!predecessor) { removed |= 1U << b; progress = true; }
      }
    }
    if (removed != kAllBlocks) {
      Check(std::get<LoopError>(result) == LoopError::kIrreducible);
      continue;
    }
    Check(std::holds_alternative<LoopForest>(result));
    const auto& forest = std::get<LoopForest>(result);
    std::array<std::optional<LoopId>, kBlocks> headers;
    for (std::uint32_t i = 0; i < forest.loops.size(); ++i) {
      const auto h = forest.loops[i].header.value;
      Check(!headers[h] && members[h] != 0); headers[h] = LoopId{i};
    }
    for (std::uint32_t h = 0; h < kBlocks; ++h) Check(headers[h].has_value() == (members[h] != 0));
    for (std::uint32_t b = 0; b < kBlocks; ++b) {
      std::optional<LoopId> owner;
      std::optional<LoopId> parent;
      unsigned owner_size = kBlocks + 1;
      unsigned parent_size = kBlocks + 1;
      for (std::uint32_t h = 0; h < kBlocks; ++h) {
        if (!(members[h] & (1U << b))) continue;
        const auto size = static_cast<unsigned>(std::popcount(members[h]));
        if (size < owner_size) { owner = headers[h]; owner_size = size; }
        if (h != b && size < parent_size) { parent = headers[h]; parent_size = size; }
      }
      Check(forest.owners[b] == owner);
      if (headers[b]) Check(forest.loops[headers[b]->value].parent == parent);
    }
    for (std::size_t e = 0; e < graph.edges.size(); ++e)
      Check(forest.backedges[e] == (backs[e] ? headers[graph.edges[e].target.value] : std::optional<LoopId>{}));
  }
}
}  // namespace
int main() { Shapes(); DeepNesting(); SmallGraphs(); }
