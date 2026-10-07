#include "abcd/analysis/dominators.h"

#include <cstdlib>
#include <optional>
#include <random>
#include <vector>

namespace {
using namespace abcd::analysis;
void Check(bool condition) {
  if (!condition) std::abort();
}
void Edge(NormalFlow& graph, std::uint32_t from, std::uint32_t to) {
  auto& block = graph.blocks[from];
  block.successors[block.successor_count++] = {BlockId{to},
                                               FlowEdgeKind::kTaken};
}
std::vector<bool> Reach(const NormalFlow& graph, std::uint32_t entry,
                        std::optional<std::uint32_t> removed = {}) {
  std::vector<bool> seen(graph.blocks.size());
  std::vector<std::uint32_t> pending;
  if (entry != removed) {
    seen[entry] = true;
    pending.push_back(entry);
  }
  while (!pending.empty()) {
    const auto node = pending.back();
    pending.pop_back();
    const auto& block = graph.blocks[node];
    for (std::size_t i = 0; i < block.successor_count; ++i) {
      const auto target = block.successors[i].target.value;
      if (!seen[target] && target != removed) {
        seen[target] = true;
        pending.push_back(target);
      }
    }
  }
  return seen;
}
void CompareOracle(const NormalFlow& graph, std::uint32_t entry) {
  const auto tree =
      std::get<DominatorTree>(BuildNormalDominators(graph, {entry}));
  std::vector<std::uint32_t> offsets{0};std::vector<BlockId> successors;
  for(const auto& block:graph.blocks) {
    for(unsigned i=0;i<block.successor_count;++i)successors.push_back(block.successors[i].target);
    offsets.push_back(static_cast<std::uint32_t>(successors.size()));
  }
  const auto direct=std::get<DominatorTree>(BuildDominators({offsets,successors},{entry}));
  const auto reachable = Reach(graph, entry);
  const auto count = graph.blocks.size();
  std::vector<std::vector<bool>> dominates(count, std::vector<bool>(count));
  for (std::uint32_t a = 0; a < count; ++a) {
    const auto without = Reach(graph, entry, a);
    for (std::uint32_t b = 0; b < count; ++b) {
      dominates[a][b] = reachable[a] && reachable[b] && !without[b];
      Check(tree.Dominates({a}, {b}) == dominates[a][b]);
      Check(direct.Dominates({a},{b})==dominates[a][b]);
    }
  }
  for (std::uint32_t b = 0; b < count; ++b) {
    std::optional<BlockId> expected;
    for (std::uint32_t a = 0; a < count; ++a) {
      if (a == b || !dominates[a][b]) continue;
      bool deepest = true;
      for (std::uint32_t c = 0; c < count; ++c)
        if (c != b && dominates[c][b] && !dominates[c][a]) deepest = false;
      if (deepest) expected = BlockId{a};
    }
    Check(tree.nodes[b].parent == expected);
    Check(direct.nodes[b].parent==expected);
  }
}
}  // namespace

int main() {
  using namespace abcd::analysis;
  std::vector<std::uint32_t> offsets{0,2,1};std::vector<BlockId> successors{{0}};
  Check(std::get<DominatorError>(BuildDominators({offsets,successors}))==DominatorError::kInvalidEdge);
  offsets={1,1};Check(std::get<DominatorError>(BuildDominators({offsets,successors}))==DominatorError::kInvalidEdge);
  offsets={0,1};successors[0]={1};
  Check(std::get<DominatorError>(BuildDominators({offsets,successors}))==DominatorError::kInvalidEdge);
  // All three-node directed graphs allowed by the normal graph's degree bound.
  constexpr std::uint32_t kNodes = 3;
  for (std::uint32_t mask = 0; mask < (1U << (kNodes * kNodes)); ++mask) {
    NormalFlow graph;
    graph.blocks.resize(kNodes);
    bool valid = true;
    for (std::uint32_t a = 0; a < kNodes; ++a)
      for (std::uint32_t b = 0; b < kNodes; ++b)
        if (mask & (1U << (a * kNodes + b))) {
          if (graph.blocks[a].successor_count == kMaxNormalSuccessors)
            valid = false;
          else
            Edge(graph, a, b);
        }
    if (valid)
      for (std::uint32_t entry = 0; entry < kNodes; ++entry)
        CompareOracle(graph, entry);
  }
  for (const std::uint32_t count : {200U, 201U, 4096U, 65536U}) {
    NormalFlow graph;
    graph.blocks.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      if (i + 1 < count) Edge(graph, i, i + 1);
      Edge(graph, i, i / 2);
    }
    const auto tree = std::get<DominatorTree>(BuildNormalDominators(graph));
    Check(tree.work.edge_visits <= 10ULL * count);
    Check(tree.work.eval_steps <= 64ULL * count);
    for (std::uint32_t i = 1; i < count; ++i) {
      Check(tree.nodes[i].parent == BlockId{i - 1});
      Check(tree.Dominates({0}, {i}) && !tree.Dominates({i}, {0}));
    }
    Check(std::get<DominatorError>(BuildNormalDominators(graph, {}, {0})) ==
          DominatorError::kResourceLimit);
    Check(std::holds_alternative<DominatorTree>(
        BuildNormalDominators(graph, {}, {tree.work.allocated_bytes})));
    Check(tree.work.steps != 0);
    Check(std::get<DominatorError>(BuildNormalDominators(
        graph, {}, {tree.work.allocated_bytes, tree.work.steps - 1})) ==
        DominatorError::kWorkLimit);
    Check(std::holds_alternative<DominatorTree>(BuildNormalDominators(
        graph, {}, {tree.work.allocated_bytes, tree.work.steps})));
    Check(std::get<DominatorError>(BuildNormalDominators(
              graph, {}, {tree.work.allocated_bytes - 1})) ==
          DominatorError::kResourceLimit);
  }
  std::mt19937 random(0x51a);
  for (std::uint32_t trial = 0; trial < 1000; ++trial) {
    NormalFlow sample;
    sample.blocks.resize(16);
    for (std::uint32_t from = 0; from < sample.blocks.size(); ++from) {
      const auto degree = random() % (kMaxNormalSuccessors + 1);
      for (std::uint32_t i = 0; i < degree; ++i)
        Edge(sample, from, random() % sample.blocks.size());
    }
    CompareOracle(sample, random() % sample.blocks.size());
  }
  NormalFlow graph;
  Check(std::get<DominatorError>(BuildNormalDominators(graph)) ==
        DominatorError::kEmptyGraph);
  graph.blocks.resize(1);
  Check(std::get<DominatorError>(BuildNormalDominators(graph, {1})) ==
        DominatorError::kInvalidEntry);
  Edge(graph, 0, 1);
  Check(std::get<DominatorError>(BuildNormalDominators(graph)) ==
        DominatorError::kInvalidEdge);
  graph.blocks[0].successor_count = kMaxNormalSuccessors + 1;
  Check(std::get<DominatorError>(BuildNormalDominators(graph)) ==
        DominatorError::kInvalidEdge);
}
