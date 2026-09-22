#include "abcd/analysis/phi_placement.h"

#include <algorithm>
#include <bit>
#include <new>

#include "abcd/analysis/dominators.h"

namespace abcd::analysis {
std::size_t IdfIndex::AllocatedBytes() const {
  return nodes_ ? count_ * (sizeof(Node) + sizeof(BlockId)) +
                      2 * leaves_ * sizeof(std::uint32_t)
                : 0;
}

std::variant<IdfIndex, IdfError> IdfIndex::Build(const NormalFlow& graph,
                                                 BlockId entry,
                                                 IdfBudget budget) {
  for (const auto& block : graph.blocks) {
    if (block.successor_count > block.successors.size())
      return IdfError::kInvalidEdge;
    for (std::size_t i = 0; i < block.successor_count; ++i) {
      if (block.successors[i].target.value >= graph.blocks.size())
        return IdfError::kInvalidEdge;
      switch (block.successors[i].kind) {
        case FlowEdgeKind::kTaken:
        case FlowEdgeKind::kFallthrough:
          break;
        case FlowEdgeKind::kResume:
          return IdfError::kResumeEdge;
        default:
          return IdfError::kInvalidEdge;
      }
    }
  }
  auto built = BuildNormalDominators(graph, entry, {budget.bytes});
  if (const auto* error = std::get_if<DominatorError>(&built)) {
    switch (*error) {
      case DominatorError::kEmptyGraph:
        return IdfError::kEmptyGraph;
      case DominatorError::kInvalidEntry:
        return IdfError::kInvalidEntry;
      case DominatorError::kInvalidEdge:
        return IdfError::kInvalidEdge;
      case DominatorError::kResourceLimit:
        return IdfError::kResourceLimit;
    }
  }
  const auto& tree = std::get<DominatorTree>(built);
  const auto retained = tree.nodes.capacity() * sizeof(DominatorNode);
  const auto count = graph.blocks.size();
  constexpr auto kSnapshotBytes = sizeof(Node) + sizeof(BlockId);
  if (retained > budget.bytes ||
      count > (budget.bytes - retained) / kSnapshotBytes)
    return IdfError::kResourceLimit;
  const auto snapshot_bytes = count * kSnapshotBytes;
  const auto leaves = std::bit_ceil(count);
  constexpr auto kTreeBytesPerLeaf = 2 * sizeof(std::uint32_t);
  if (leaves > (budget.bytes - retained - snapshot_bytes) / kTreeBytesPerLeaf)
    return IdfError::kResourceLimit;
  try {
    IdfIndex index;
    index.count_ = count;
    index.leaves_ = leaves;
    index.nodes_ = std::make_unique<Node[]>(count);
    index.preorder_ = std::make_unique<BlockId[]>(count);
    index.min_depth_ = std::make_unique<std::uint32_t[]>(2 * leaves);
    std::fill_n(index.min_depth_.get(), 2 * leaves, kNoDepth);
    index.peak_bytes_ =
        std::max(tree.work.allocated_bytes, retained + index.AllocatedBytes());
    std::size_t reachable = 0;
    for (std::size_t i = 0; i < count; ++i) {
      auto& node = index.nodes_[i];
      node.preorder = tree.nodes[i].begin;
      node.end = tree.nodes[i].end;
      if (node.preorder != 0) {
        index.preorder_[node.preorder - 1] = {static_cast<std::uint32_t>(i)};
        ++reachable;
      }
      node.successor_count = graph.blocks[i].successor_count;
      for (std::size_t j = 0; j < node.successor_count; ++j)
        node.successors[j] = graph.blocks[i].successors[j].target;
    }
    // Dominator preorder puts every parent before its children, even when
    // physical block IDs are unrelated. No recursion or per-node parent climb.
    for (std::size_t i = 1; i < reachable; ++i) {
      const auto child = index.preorder_[i];
      const auto parent = *tree.nodes[child.value].parent;
      auto& node = index.nodes_[child.value];
      node.depth = index.nodes_[parent.value].depth + 1;
    }
    // Range minima skip dominance regions without a shallow enough CFG edge.
    // Never scan a long straight chain anew for every entry definition.
    for (std::size_t i = 0; i < reachable; ++i) {
      const auto& node = index.nodes_[index.preorder_[i].value];
      auto& minimum = index.min_depth_[leaves + i];
      for (std::size_t e = 0; e < node.successor_count; ++e)
        minimum =
            std::min(minimum, index.nodes_[node.successors[e].value].depth);
    }
    for (auto i = leaves; i > 1; --i) {
      const auto parent = i - 1;
      index.min_depth_[parent] = std::min(index.min_depth_[2 * parent],
                                          index.min_depth_[2 * parent + 1]);
    }
    return index;
  } catch (const std::bad_alloc&) {
    return IdfError::kResourceLimit;
  }
}
}  // namespace abcd::analysis
