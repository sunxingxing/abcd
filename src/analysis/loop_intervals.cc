#include "loop_intervals.h"

#include <algorithm>

namespace abcd::analysis {
std::optional<RegionFailure> FindLoopIntervals(
    const NormalFlow& flow,
    std::span<std::optional<std::uint32_t>> ends,
    std::span<std::optional<std::uint32_t>> enclosing,
    std::vector<std::uint32_t>& stack) {
  const auto& blocks = flow.blocks;
  const auto count = blocks.size();
  if (ends.size() != count || enclosing.size() != count ||
      !stack.empty() || stack.capacity() < count)
    return RegionFailure{RegionError::kResourceLimit, {}};
  for (std::size_t i = 0; i < count; ++i) {
    const auto& block = blocks[i];
    if (block.successor_count > block.successors.size())
      return RegionFailure{RegionError::kBackwardEdge, block.end};
    for (std::size_t e = 0; e < block.successor_count; ++e) {
      const auto& edge = block.successors[e];
      const auto target = edge.target.value;
      if (target >= count || edge.kind == FlowEdgeKind::kResume ||
          (edge.kind == FlowEdgeKind::kFallthrough && target != i + 1))
        return RegionFailure{RegionError::kBackwardEdge, block.end};
      if (target <= i)
        ends[target] = std::max(ends[target].value_or(target),
                               static_cast<std::uint32_t>(i));
    }
  }
  // A backward edge alone is insufficient: crossing intervals and entries
  // into a loop interior cannot be printed as lexical continue/break labels.
  for (std::uint32_t i = 0; i < count; ++i) {
    while (!stack.empty() && *ends[stack.back()] < i) stack.pop_back();
    if (!stack.empty()) enclosing[i] = stack.back();
    if (ends[i]) {
      if (!stack.empty() && *ends[i] > *ends[stack.back()])
        return RegionFailure{RegionError::kCycle, blocks[i].begin};
      stack.push_back(i);
    }
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto& block = blocks[i];
    for (std::size_t e = 0; e < block.successor_count; ++e) {
      const auto owner = enclosing[block.successors[e].target.value];
      if (owner && (i < *owner || i > *ends[*owner]))
        return RegionFailure{RegionError::kCycle, block.end};
    }
  }
  stack.clear();
  return std::nullopt;
}
}  // namespace abcd::analysis
