#include <functional>
#include <random>

#include "abcd/analysis/block_order.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
void VerifyBlockLoopOrder() {
  using namespace analysis;
  constexpr std::size_t kBlocks = 8;
  std::mt19937 random{90417};
  for (std::size_t trial = 0; trial < 1000; ++trial) {
    NormalFlow graph;
    graph.blocks.resize(kBlocks);
    for (auto& block : graph.blocks) {
      block.successor_count = random() % (kMaxNormalSuccessors + 1);
      for (std::size_t i = 0; i < block.successor_count; ++i)
        block.successors[i].target.value = random() % kBlocks;
    }
    // Small independent DFS oracle ignores self edges, detects other cycles.
    std::array<std::uint8_t, kBlocks> color{};
    bool cyclic = false;
    std::function<void(std::size_t)> visit = [&](std::size_t id) {
      if (color[id] == 1) {
        cyclic = true;
        return;
      }
      if (color[id] == 2) return;
      color[id] = 1;
      const auto& block = graph.blocks[id];
      for (std::size_t i = 0; i < block.successor_count; ++i)
        if (block.successors[i].target.value != id)
          visit(block.successors[i].target.value);
      color[id] = 2;
    };
    visit(0);
    const auto actual = OrderBlockLoops(graph);
    if (cyclic) {
      Check(std::get<OrderError>(actual) == OrderError::kCycle);
      continue;
    }
    const auto& order = std::get<BlockOrder>(actual);
    std::array<std::size_t, kBlocks> position;
    position.fill(kBlocks);
    for (std::size_t i = 0; i < order.blocks.size(); ++i)
      position[order.blocks[i].value] = i;
    for (std::size_t id = 0; id < kBlocks; ++id) {
      Check((position[id] != kBlocks) == (color[id] != 0));
      if (color[id] == 0) continue;
      const auto& block = graph.blocks[id];
      for (std::size_t i = 0; i < block.successor_count; ++i) {
        const auto target = block.successors[i].target.value;
        if (target != id) Check(position[id] < position[target]);
      }
    }
    Check(order.work.edge_visits <= kBlocks * kMaxNormalSuccessors * 3);
  }
  NormalFlow graph;
  graph.blocks.resize(1);
  graph.blocks[0].successor_count = 2;
  Check(std::get<BlockOrder>(OrderBlockLoops(graph)).blocks.size() == 1);
  Check(std::get<OrderError>(OrderAcyclicBlocks(graph)) == OrderError::kCycle);
  Check(std::get<OrderError>(OrderBlockLoops(graph, {}, {0})) ==
        OrderError::kResourceLimit);
  graph.blocks[0].successors[0].kind = FlowEdgeKind::kResume;
  Check(std::get<OrderError>(OrderBlockLoops(graph)) ==
        OrderError::kResumeEdge);
  graph.blocks[0].successors[0].target.value = 1;
  Check(std::get<OrderError>(OrderBlockLoops(graph)) ==
        OrderError::kInvalidEdge);
}
}  // namespace abcd::test
