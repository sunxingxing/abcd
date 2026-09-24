#include "abcd/analysis/semantic_region.h"

#include <new>
#include <stdexcept>
#include <utility>

#include "abcd/analysis/block_order.h"

namespace abcd::analysis {
namespace {
constexpr std::size_t kMaxRegionDepth = 128;
constexpr std::size_t kNodesPerBlock = 4;
RegionFailure Fail(RegionError kind, abc::CodeOffset pc = {}) {
  return {kind, pc};
}
}  // namespace

RegionResult BuildSemanticRegions(const SemanticMethod& method,
                                  std::size_t budget_bytes) {
  const auto ordered = OrderAcyclicBlocks(method.flow, {}, {budget_bytes});
  if (const auto* error = std::get_if<OrderError>(&ordered)) {
    const auto kind = *error == OrderError::kCycle
                          ? RegionError::kCycle
                          : *error == OrderError::kResourceLimit
                                ? RegionError::kResourceLimit
                                : RegionError::kBackwardEdge;
    return Fail(kind);
  }
  const auto& order = std::get<BlockOrder>(ordered);
  if (order.unreachable != 0 ||
      order.blocks.size() != method.flow.blocks.size())
    return Fail(RegionError::kUnreachable);
  if (order.blocks.size() > kMaxRegionDepth)
    return Fail(RegionError::kDepth);
  if (method.flow.blocks.size() >
      budget_bytes / (sizeof(std::uint32_t) +
                      kNodesPerBlock * sizeof(RegionNode)))
    return Fail(RegionError::kResourceLimit);
  try {
    SemanticRegionTree tree;
    tree.order_positions.resize(method.flow.blocks.size());
    for (std::size_t i = 0; i < order.blocks.size(); ++i)
      tree.order_positions[order.blocks[i].value] = i;
    for (std::size_t i = 0; i < order.blocks.size(); ++i) {
      const auto id = order.blocks[i];
      const auto& block = method.flow.blocks[id.value];
      for (std::size_t edge = 0; edge < block.successor_count; ++edge) {
        const auto target = block.successors[edge].target;
        if (target.value >= tree.order_positions.size() ||
            tree.order_positions[target.value] <= i)
          return Fail(RegionError::kBackwardEdge, block.end);
      }
    }
    RegionNode root{RegionBlock{order.blocks.front()}};
    for (std::size_t i = 1; i < order.blocks.size(); ++i) {
      RegionSequence sequence;
      sequence.children.reserve(2);
      sequence.children.push_back(
          {RegionLabel{order.blocks[i],
                       std::make_unique<RegionNode>(std::move(root))}});
      sequence.children.push_back({RegionBlock{order.blocks[i]}});
      root = RegionNode{std::move(sequence)};
    }
    tree.root = std::move(root);
    tree.allocated_bytes = tree.order_positions.capacity() *
                               sizeof(std::uint32_t) +
                           order.blocks.size() * kNodesPerBlock *
                               sizeof(RegionNode);
    return tree;
  } catch (const std::bad_alloc&) {
    return Fail(RegionError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(RegionError::kResourceLimit);
  }
}
}  // namespace abcd::analysis
