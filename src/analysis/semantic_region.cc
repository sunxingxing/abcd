#include "abcd/analysis/semantic_region.h"

#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

#include "abcd/analysis/block_order.h"
#include "loop_intervals.h"
#include "region_if.h"

namespace abcd::analysis {
namespace {
constexpr std::size_t kNodesPerBlock = 4;
RegionFailure Fail(RegionError kind, abc::CodeOffset pc = {}) {
  return {kind, pc};
}

RegionResult BuildLoopRegions(const SemanticMethod& method,
                             std::size_t budget_bytes) {
  const auto& blocks = method.flow.blocks;
  const auto count = blocks.size();
  // The printer's region nesting remains bounded independently of graph size.
  if (count > kMaxSemanticRegionDepth) return Fail(RegionError::kDepth);
  constexpr auto kLoopNodesPerBlock = kNodesPerBlock + 2;
  constexpr auto kBlockBytes = kLoopNodesPerBlock * sizeof(RegionNode) +
      3 * sizeof(std::uint32_t) + sizeof(std::uint8_t) + sizeof(EntryCount) +
      2 * sizeof(std::optional<std::uint32_t>);
  if (!count || count > budget_bytes / kBlockBytes)
    return Fail(RegionError::kResourceLimit);
  try {
    std::vector<std::optional<std::uint32_t>> ends(count), enclosing(count);
    std::vector<std::uint32_t> pending;
    pending.reserve(count);
    std::vector<std::uint8_t> reached(count);
    std::vector<EntryCount> entries(count);
    if (ends.capacity() > count || enclosing.capacity() > count ||
        pending.capacity() > count || reached.capacity() > count || entries.capacity() > count)
      return Fail(RegionError::kResourceLimit);
    if (const auto failure = CountRegionEntries(method.flow, entries)) return *failure;
    if (const auto failure = FindLoopIntervals(method.flow, ends, enclosing, pending))
      return *failure;
    reached[0] = 1;
    pending.push_back(0);
    for (std::size_t head = 0; head < pending.size(); ++head) {
      const auto& block = blocks[pending[head]];
      for (std::size_t e = 0; e < block.successor_count; ++e) {
        const auto target = block.successors[e].target.value;
        if (!reached[target]) {
          reached[target] = 1;
          pending.push_back(target);
        }
      }
    }
    if (pending.size() != count) return Fail(RegionError::kUnreachable);
    pending.clear();
    // Intervals are nested or disjoint; each block and label is built once.
    // A loop header has a break label outside its loop for forward entries,
    // and a distinct continue label inside it for proven backedges.
    const auto sequence = [&](auto&& self, std::uint32_t first,
                              std::uint32_t last, bool loop_body) -> RegionNode {
      std::optional<RegionNode> root;
      for (auto i = first; i <= last;) {
        const auto id = i;
        RegionNode node{RegionBlock{BlockId{id}}};
        if (ends[i] && !(loop_body && i == first)) {
          node = RegionNode{RegionLoop{BlockId{id},
              std::make_unique<RegionNode>(self(self, i, *ends[i], true))}};
          i = *ends[i] + 1;
        } else if (i + kIfElseBlocks <= last &&
                   CanFormIf(method.flow, entries, BlockId{i}, BlockId{i + 1},
                             BlockId{i + kIfElseBlocks}, BlockId{i + kIfBlocks})) {
          node = RegionNode{RegionIf{BlockId{i}, BlockId{i + 1}, BlockId{i + kIfElseBlocks},
                                     BlockId{i + kIfBlocks}}};
          i += kIfElseBlocks;
        } else if (i + kIfBlocks <= last &&
                   CanFormIf(method.flow, entries, BlockId{i}, BlockId{i + 1},
                             BlockId{i + kIfBlocks})) {
          node = RegionNode{RegionIf{BlockId{i}, BlockId{i + 1}, BlockId{i + kIfBlocks}}};
          i += kIfBlocks;
        } else {
          ++i;
        }
        if (!root) {
          root = std::move(node);
        } else {
          RegionSequence joined;
          joined.children.reserve(2);
          joined.children.push_back({RegionLabel{BlockId{id},
              std::make_unique<RegionNode>(std::move(*root))}});
          joined.children.push_back(std::move(node));
          root = RegionNode{std::move(joined)};
        }
      }
      return std::move(*root);
    };
    SemanticRegionTree tree;
    tree.order_positions.resize(count);
    if (tree.order_positions.capacity() > count)
      return Fail(RegionError::kResourceLimit);
    for (std::uint32_t i = 0; i < count; ++i) tree.order_positions[i] = i;
    tree.root = sequence(sequence, 0, static_cast<std::uint32_t>(count - 1), false);
    tree.allocated_bytes = tree.order_positions.capacity() * sizeof(std::uint32_t) +
        count * kLoopNodesPerBlock * sizeof(RegionNode);
    return tree;
  } catch (const std::bad_alloc&) {
    return Fail(RegionError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(RegionError::kResourceLimit);
  }
}
}  // namespace

RegionResult BuildSemanticRegions(const SemanticMethod& method,
                                  std::size_t budget_bytes) {
  const auto ordered = OrderAcyclicBlocks(method.flow, {}, {budget_bytes});
  if (const auto* error = std::get_if<OrderError>(&ordered)) {
    if (*error == OrderError::kCycle) return BuildLoopRegions(method, budget_bytes);
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
  if (order.blocks.size() > kMaxSemanticRegionDepth)
    return Fail(RegionError::kDepth);
  if (method.flow.blocks.size() >
      budget_bytes / (sizeof(std::uint32_t) + sizeof(EntryCount) +
                      kNodesPerBlock * sizeof(RegionNode)))
    return Fail(RegionError::kResourceLimit);
  try {
    SemanticRegionTree tree;
    std::vector<EntryCount> entries(method.flow.blocks.size());
    if (entries.capacity() > method.flow.blocks.size()) return Fail(RegionError::kResourceLimit);
    if (const auto failure = CountRegionEntries(method.flow, entries)) return *failure;
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
    std::optional<RegionNode> root;
    for (std::size_t i = 0; i < order.blocks.size();) {
      const auto start = order.blocks[i];
      RegionNode node{RegionBlock{start}};
      if (i + kIfElseBlocks < order.blocks.size() &&
          CanFormIf(method.flow, entries, start, order.blocks[i + 1],
                    order.blocks[i + kIfElseBlocks], order.blocks[i + kIfBlocks])) {
        node = RegionNode{RegionIf{start, order.blocks[i + 1], order.blocks[i + kIfElseBlocks],
                                   order.blocks[i + kIfBlocks]}};
        i += kIfElseBlocks;
      } else if (i + kIfBlocks < order.blocks.size() &&
          CanFormIf(method.flow, entries, start, order.blocks[i + 1],
                    order.blocks[i + kIfBlocks])) {
        node = RegionNode{RegionIf{start, order.blocks[i + 1], order.blocks[i + kIfBlocks]}};
        i += kIfBlocks;
      } else ++i;
      if (!root) root = std::move(node);
      else {
        RegionSequence sequence;
        sequence.children.reserve(2);
        sequence.children.push_back({RegionLabel{start,
            std::make_unique<RegionNode>(std::move(*root))}});
        sequence.children.push_back(std::move(node));
        root = RegionNode{std::move(sequence)};
      }
    }
    tree.root = std::move(*root);
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
