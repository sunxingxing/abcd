#include "abcd/analysis/block_order.h"

#include <new>

namespace abcd::analysis {
std::variant<BlockOrder, OrderError> OrderAcyclicBlocks(const NormalFlow& graph,
                                                        BlockId entry,
                                                        OrderBudget budget) {
  const auto count = graph.blocks.size();
  if (count == 0) return OrderError::kEmptyGraph;
  if (entry.value >= count) return OrderError::kInvalidEntry;
  if (count > UINT32_MAX) return OrderError::kResourceLimit;
  BlockOrder result;
  for (const auto& block : graph.blocks) {
    if (block.successor_count > block.successors.size())
      return OrderError::kInvalidEdge;
    for (std::size_t i = 0; i < block.successor_count; ++i) {
      const auto edge = block.successors[i];
      ++result.work.edge_visits;
      if (edge.target.value >= count) return OrderError::kInvalidEdge;
      switch (edge.kind) {
        case FlowEdgeKind::kResume:
          return OrderError::kResumeEdge;
        case FlowEdgeKind::kTaken:
        case FlowEdgeKind::kFallthrough:
          break;
        default:
          return OrderError::kInvalidEdge;
      }
    }
  }
  try {
    std::vector<std::uint8_t> seen;
    std::vector<std::uint64_t> incoming;
    std::vector<BlockId> discovery;
    const auto allocate = [&]<class T>(std::vector<T>& storage) {
      auto& spent = result.work.allocated_bytes;
      if (count > storage.max_size() ||
          count > (budget.bytes - spent) / sizeof(T))
        return false;
      storage.resize(count);
      if (storage.capacity() > (budget.bytes - spent) / sizeof(T)) return false;
      spent += storage.capacity() * sizeof(T);
      return true;
    };
    if (!allocate(seen) || !allocate(incoming) || !allocate(discovery) ||
        !allocate(result.blocks))
      return OrderError::kResourceLimit;
    std::size_t reachable = 1;
    discovery[0] = entry;
    seen[entry.value] = true;
    for (std::size_t cursor = 0; cursor < reachable; ++cursor) {
      const auto& block = graph.blocks[discovery[cursor].value];
      for (std::size_t i = 0; i < block.successor_count; ++i) {
        ++result.work.edge_visits;
        const auto target = block.successors[i].target;
        ++incoming[target.value];
        if (!seen[target.value]) {
          seen[target.value] = true;
          discovery[reachable++] = target;
        }
      }
    }
    // Reuse the result as the ready FIFO. Each block is queued exactly once
    // when its incoming reachable-edge count reaches zero. Parallel edges
    // are counted and removed individually; no predecessor deduplication.
    std::size_t ready = 0;
    for (std::size_t i = 0; i < count; ++i)
      if (seen[i] && incoming[i] == 0)
        result.blocks[ready++] = {static_cast<std::uint32_t>(i)};
    for (std::size_t cursor = 0; cursor < ready; ++cursor) {
      const auto& block = graph.blocks[result.blocks[cursor].value];
      for (std::size_t i = 0; i < block.successor_count; ++i) {
        ++result.work.edge_visits;
        const auto target = block.successors[i].target;
        if (--incoming[target.value] == 0) result.blocks[ready++] = target;
      }
    }
    if (ready != reachable) return OrderError::kCycle;
    result.blocks.resize(ready);
    result.unreachable = count - reachable;
    return result;
  } catch (const std::bad_alloc&) {
    return OrderError::kResourceLimit;
  }
}
}  // namespace abcd::analysis
