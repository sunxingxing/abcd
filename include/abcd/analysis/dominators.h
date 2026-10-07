#ifndef ABCD_ANALYSIS_DOMINATORS_H_
#define ABCD_ANALYSIS_DOMINATORS_H_

#include <optional>
#include <span>

#include "abcd/analysis/normal_flow.h"

namespace abcd::analysis {
struct DominatorNode {
  std::optional<BlockId> parent;
  // Zero means unreachable. Reachable subtrees occupy [begin, end).
  std::uint32_t begin = 0;
  std::uint32_t end = 0;
};
struct DominatorWork {
  std::uint64_t steps = 0;
  std::uint64_t edge_visits = 0;
  std::uint64_t eval_steps = 0;
  std::size_t allocated_bytes = 0;
};
struct DominatorTree {
  std::vector<DominatorNode> nodes;
  DominatorWork work;
  [[nodiscard]] bool Dominates(BlockId ancestor, BlockId descendant) const;
};
enum class DominatorError {
  kEmptyGraph,
  kInvalidEntry,
  kInvalidEdge,
  kWorkLimit,
  kResourceLimit
};
struct DominatorBudget {
  std::size_t bytes = 64 * 1024 * 1024;
  // Existing input analyzers retain their historical allowance when absent.
  // Working-IR verification supplies its remaining shared work quota.
  std::optional<std::uint64_t> work;
};
using DominatorResult = std::variant<DominatorTree, DominatorError>;

// Simple Lengauer-Tarjan with iterative path compression: O((B+E) log B),
// O(B+E) additional storage. Budget includes result and simultaneous scratch.
// Includes the supplied normal/resume edges, NOT missing exception dispatch.
// Root and unreachable nodes have no parent; begin distinguishes reachability.
// Do not use this normal-only result as dominance over exceptional execution.
[[nodiscard]] DominatorResult BuildNormalDominators(
    const NormalFlow& graph, BlockId entry = {}, DominatorBudget budget = {});
// General completion graphs use CSR adjacency without synthetic binary-fanout
// nodes. The same LT implementation and budgets apply to every supplied edge.
struct DominatorAdjacency {
  std::span<const std::uint32_t> offsets;
  std::span<const BlockId> successors;
};
[[nodiscard]] DominatorResult BuildDominators(
    DominatorAdjacency graph,BlockId entry = {},DominatorBudget budget = {});
}  // namespace abcd::analysis
#endif
