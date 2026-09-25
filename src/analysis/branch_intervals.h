#ifndef ABCD_ANALYSIS_BRANCH_INTERVALS_H_
#define ABCD_ANALYSIS_BRANCH_INTERVALS_H_

#include <algorithm>
#include <limits>
#include <span>

#include "abcd/analysis/semantic_region.h"

namespace abcd::analysis {
inline constexpr std::size_t kBranchTreeChildren = 2;
struct BranchInterval {
  std::uint32_t alternate;
  std::uint32_t join;
};
struct BranchEdges {
  static constexpr auto kNoPredecessor = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t first_predecessor = kNoPredecessor;
  std::optional<std::uint32_t> last;
  std::optional<std::uint32_t> second;
  void Add(std::uint32_t target) {
    if (!last || target > *last) { second = last; last = target; }
    else if (target != *last && (!second || target > *second)) second = target;
  }
};
inline BranchEdges JoinEdges(BranchEdges left, const BranchEdges& right) {
  left.first_predecessor = std::min(left.first_predecessor, right.first_predecessor);
  if (right.last) left.Add(*right.last);
  if (right.second) left.Add(*right.second);
  return left;
}
// Iterative range-query tree: minimum predecessor and two largest distinct
// successors prove interval isolation without rescanning each branch's body.
// The complete forward order has already been validated by the caller.
inline bool FindBranchIntervals(const NormalFlow& flow,
    std::span<const BlockId> order, std::span<const std::uint32_t> positions,
    std::span<std::optional<BranchInterval>> intervals,
    std::span<BranchEdges> tree) {
  const auto count = order.size();
  if (!count || intervals.size() != count || tree.size() != kBranchTreeChildren * count)
    return false;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto& block = flow.blocks[order[i].value];
    for (std::size_t e = 0; e < block.successor_count; ++e) {
      const auto target = positions[block.successors[e].target.value];
      tree[count + i].Add(target);
      auto& incoming = tree[count + target].first_predecessor;
      incoming = std::min(incoming, i);
    }
  }
  for (auto i = count; --i > 0;)
    tree[i] = JoinEdges(tree[i * kBranchTreeChildren], tree[i * kBranchTreeChildren + 1]);
  const auto query = [&](std::size_t first, std::size_t end) {
    BranchEdges result;
    for (first += count, end += count; first < end;
         first /= kBranchTreeChildren, end /= kBranchTreeChildren) {
      if (first % kBranchTreeChildren) result = JoinEdges(result, tree[first++]);
      if (end % kBranchTreeChildren) result = JoinEdges(result, tree[--end]);
    }
    return result;
  };
  for (std::uint32_t h = 0; h < count; ++h) {
    const auto& header = flow.blocks[order[h].value];
    if (header.transfer != abc::ControlTransfer::kBranch ||
        header.successor_count != kMaxNormalSuccessors) continue;
    std::optional<std::uint32_t> body, alternate;
    for (std::size_t e = 0; e < header.successor_count; ++e) {
      const auto& edge = header.successors[e];
      if (edge.kind == FlowEdgeKind::kFallthrough) body = positions[edge.target.value];
      if (edge.kind == FlowEdgeKind::kTaken) alternate = positions[edge.target.value];
    }
    if (!body || !alternate || *body != h + 1 || *alternate <= *body) continue;
    const auto left = query(*body, *alternate);
    if (left.first_predecessor < h || !left.last || *left.last <= *alternate ||
        *left.last >= count || (left.second && *left.second >= *alternate)) continue;
    const auto join = *left.last;
    const auto right = query(*alternate, join);
    if (right.first_predecessor < h || !right.last || *right.last != join) continue;
    // Body edges leave only at join; alternate edges also stay inside or join.
    // No outside predecessor enters either interval, except header's two edges.
    intervals[h] = BranchInterval{*alternate, join};
  }
  return true;
}
inline constexpr std::size_t kBranchIntervalBytesPerBlock =
    sizeof(std::optional<BranchInterval>) + kBranchTreeChildren * sizeof(BranchEdges);
}  // namespace abcd::analysis
#endif
