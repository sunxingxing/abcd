#include "abcd/analysis/block_order.h"

#include <cstdlib>

namespace {
using namespace abcd::analysis;
void Check(bool condition) {
  if (!condition) std::abort();
}
void Edge(NormalFlow& graph, std::uint32_t from, std::uint32_t to) {
  auto& block = graph.blocks[from];
  block.successors[block.successor_count++] = {{to},
                                               FlowEdgeKind::kFallthrough};
}
void Verify(const NormalFlow& graph, const BlockOrder& order) {
  std::vector<std::size_t> ranks(graph.blocks.size(), graph.blocks.size());
  for (std::size_t i = 0; i < order.blocks.size(); ++i) {
    Check(ranks[order.blocks[i].value] == graph.blocks.size());
    ranks[order.blocks[i].value] = i;
  }
  for (const auto id : order.blocks)
    for (std::size_t i = 0; i < graph.blocks[id.value].successor_count; ++i)
      Check(ranks[id.value] <
            ranks[graph.blocks[id.value].successors[i].target.value]);
}
}  // namespace
int main() {
  // Exhaustive tiny-graph oracle intentionally uses a dense transitive closure.
  // Production never constructs a B-by-B matrix.
  constexpr std::uint32_t kOracleNodes = 3;
  constexpr std::uint32_t kGraphMasks = 1U << (kOracleNodes * kOracleNodes);
  for (std::uint32_t mask = 0; mask < kGraphMasks; ++mask) {
    NormalFlow small;
    small.blocks.resize(kOracleNodes);
    bool reach[kOracleNodes][kOracleNodes]{};
    bool fits = true;
    for (std::uint32_t from = 0; from < kOracleNodes; ++from)
      for (std::uint32_t to = 0; to < kOracleNodes; ++to)
        if ((mask & (1U << (from * kOracleNodes + to))) != 0) {
          reach[from][to] = true;
          if (small.blocks[from].successor_count == kMaxNormalSuccessors)
            fits = false;
          else
            Edge(small, from, to);
        }
    if (!fits) continue;
    for (std::uint32_t via = 0; via < kOracleNodes; ++via)
      for (std::uint32_t from = 0; from < kOracleNodes; ++from)
        for (std::uint32_t to = 0; to < kOracleNodes; ++to)
          reach[from][to] |= reach[from][via] && reach[via][to];
    bool cycle = false;
    std::size_t reachable = 0;
    for (std::uint32_t i = 0; i < kOracleNodes; ++i)
      if (i == 0 || reach[0][i]) {
        ++reachable;
        cycle |= reach[i][i];
      }
    const auto sorted = OrderAcyclicBlocks(small);
    if (cycle) {
      Check(std::get<OrderError>(sorted) == OrderError::kCycle);
    } else {
      const auto& order = std::get<BlockOrder>(sorted);
      Check(order.blocks.size() == reachable);
      Verify(small, order);
    }
  }
  Check(std::get<OrderError>(OrderAcyclicBlocks({})) ==
        OrderError::kEmptyGraph);
  NormalFlow graph;
  graph.blocks.resize(5);
  Edge(graph, 3, 1);
  Edge(graph, 3, 2);
  Edge(graph, 1, 0);
  Edge(graph, 2, 0);
  Edge(graph, 4, 4);  // Unreachable normal-flow cycle is not a reachable cycle.
  auto result = OrderAcyclicBlocks(graph, {3});
  const auto& order = std::get<BlockOrder>(result);
  Check(order.blocks.front().value == 3 && order.blocks.back().value == 0);
  Check(order.blocks.size() == 4 && order.unreachable == 1);
  Verify(graph, order);
  Check(std::holds_alternative<BlockOrder>(
      OrderAcyclicBlocks(graph, {3}, {order.work.allocated_bytes})));
  Check(std::get<OrderError>(
            OrderAcyclicBlocks(graph, {3}, {order.work.allocated_bytes - 1})) ==
        OrderError::kResourceLimit);
  Check(std::get<OrderError>(OrderAcyclicBlocks(graph, {5})) ==
        OrderError::kInvalidEntry);
  Edge(graph, 0, 3);
  Check(std::get<OrderError>(OrderAcyclicBlocks(graph, {3})) ==
        OrderError::kCycle);
  graph.blocks[0].successor_count = 0;
  graph.blocks[4].successors[0].kind = FlowEdgeKind::kResume;
  Check(std::get<OrderError>(OrderAcyclicBlocks(graph, {3})) ==
        OrderError::kResumeEdge);
  graph.blocks[4].successors[0].kind = FlowEdgeKind::kFallthrough;
  graph.blocks[4].successors[0].target = {5};
  Check(std::get<OrderError>(OrderAcyclicBlocks(graph, {3})) ==
        OrderError::kInvalidEdge);
  graph.blocks[4].successor_count = 3;
  Check(std::get<OrderError>(OrderAcyclicBlocks(graph, {3})) ==
        OrderError::kInvalidEdge);
  for (const std::uint32_t count : {1U, 200U, 201U, 4096U, 65536U}) {
    NormalFlow chain;
    chain.blocks.resize(count);
    for (std::uint32_t i = 1; i < count; ++i) {
      Edge(chain, i, i - 1);
      Edge(chain, i,
           i - 1);  // Parallel branch edges are counted, not collapsed.
    }
    const auto sorted =
        std::get<BlockOrder>(OrderAcyclicBlocks(chain, {count - 1}));
    Verify(chain, sorted);
    Check(sorted.blocks.size() == count && sorted.unreachable == 0);
    Check(sorted.work.edge_visits == 6 * (std::uint64_t(count) - 1));
  }
}
