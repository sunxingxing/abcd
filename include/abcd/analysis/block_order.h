#ifndef ABCD_ANALYSIS_BLOCK_ORDER_H_
#define ABCD_ANALYSIS_BLOCK_ORDER_H_

#include "abcd/analysis/normal_flow.h"

namespace abcd::analysis {
struct OrderWork {
  std::uint64_t edge_visits = 0;
  std::size_t allocated_bytes = 0;
};
struct BlockOrder {
  std::vector<BlockId> blocks;
  std::size_t unreachable = 0;
  OrderWork work;
};
enum class OrderError {
  kEmptyGraph,
  kInvalidEntry,
  kInvalidEdge,
  kResumeEdge,
  kCycle,
  kResourceLimit
};
struct OrderBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};

// Kahn ordering of the entry-reachable normal graph. All edges, including
// unreachable ones, are validated; resume edges require a different activation
// model and are rejected. O(B+E) time, O(B) storage, no recursion. Budget
// covers simultaneous vector capacities (result + scratch), not the caller's
// graph. Missing exceptional edges are NOT inferred: unreachable here does not
// mean dead in the program. This is analysis order, NOT a permitted reordering
// of instructions or permission to discard blocks from recovered source.
[[nodiscard]] std::variant<BlockOrder, OrderError> OrderAcyclicBlocks(
    const NormalFlow& graph, BlockId entry = {}, OrderBudget budget = {});
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_BLOCK_ORDER_H_
