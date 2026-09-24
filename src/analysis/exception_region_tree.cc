#include "abcd/analysis/semantic_region.h"

#include <new>
#include <stdexcept>

namespace abcd::analysis {
namespace {
struct CatchBlocks {
  BlockId begin;
  BlockId end;
  BlockId handler_begin;
  BlockId handler_end;
};
}
RegionResult BuildExceptionRegionTree(const ExceptionFlow& flow,
    const ExceptionRegions& regions, const NormalFlow& normal,
    std::size_t budget_bytes) {
  const auto fail = [](RegionError kind, abc::CodeOffset pc = {}) {
    return RegionFailure{kind, pc};
  };
  const auto count = normal.blocks.size();
  const auto catches = regions.regions.size();
  if (!count || count > kMaxSemanticRegionDepth ||
      catches > (kMaxSemanticRegionDepth - count) / 2)
    return fail(RegionError::kDepth);
  if (regions.source_order.size() != catches ||
      regions.scopes.size() != flow.instructions.size())
    return fail(RegionError::kExceptionShape);
  // Each sequence join uses a label and two children. Catch adds its node,
  // two owned subtrees and an optional skip label. These are upper bounds,
  // admitted together with all scratch before recursive construction.
  constexpr std::size_t kNodesPerBlock = 4;
  constexpr std::size_t kNodesPerCatch = 4;
  const auto tree_bytes = (count * kNodesPerBlock + catches * kNodesPerCatch) * sizeof(RegionNode) +
      count * sizeof(std::uint32_t);
  if (tree_bytes > budget_bytes) return fail(RegionError::kResourceLimit);
  auto available = budget_bytes - tree_bytes;
  const auto map_count = flow.instructions.size() + 1;
  if (map_count > available / sizeof(std::optional<BlockId>))
    return fail(RegionError::kResourceLimit);
  available -= map_count * sizeof(std::optional<BlockId>);
  if (catches > available / sizeof(CatchBlocks)) return fail(RegionError::kResourceLimit);
  available -= catches * sizeof(CatchBlocks);
  if (count > available / sizeof(std::optional<TryId>)) return fail(RegionError::kResourceLimit);
  try {
    std::vector<std::optional<BlockId>> boundaries(map_count);
    std::vector<CatchBlocks> blocks(catches);
    std::vector<std::optional<TryId>> gaps(count);
    if (boundaries.capacity() > map_count || blocks.capacity() > catches || gaps.capacity() > count)
      return fail(RegionError::kResourceLimit);
    std::size_t covered = 0;
    for (std::size_t i = 0; i < count; ++i) {
      const auto& block = normal.blocks[i];
      if (block.first_instruction != covered || !block.instruction_count ||
          block.instruction_count > flow.instructions.size() - covered ||
          block.successor_count > block.successors.size())
        return fail(RegionError::kExceptionShape, block.begin);
      boundaries[covered] = BlockId{static_cast<std::uint32_t>(i)};
      covered += block.instruction_count;
      for (std::size_t e = 0; e < block.successor_count; ++e)
        if (block.successors[e].target.value <= i ||
            block.successors[e].target.value >= count ||
            block.successors[e].kind == FlowEdgeKind::kResume)
          return fail(RegionError::kBackwardEdge, block.end);
    }
    if (covered != flow.instructions.size()) return fail(RegionError::kExceptionShape);
    boundaries[covered] = BlockId{static_cast<std::uint32_t>(count)};
    const auto at = [&](InstructionId instruction) {
      if (instruction.value >= boundaries.size() || !boundaries[instruction.value])
        throw fail(RegionError::kExceptionShape);
      return *boundaries[instruction.value];
    };
    for (std::size_t i = 0; i < catches; ++i) {
      const auto& region = regions.regions[i];
      blocks[i] = {at(region.body.begin), at(region.body.end),
                   at(region.handler.begin), at(region.handler.end)};
      const auto& block = blocks[i];
      if (block.begin >= block.end || block.end > block.handler_begin ||
          block.handler_begin >= block.handler_end)
        return fail(RegionError::kExceptionShape);
      if (block.end == block.handler_begin) continue;
      if (block.end.value + 1 != block.handler_begin.value ||
          region.body.end.value + 1 != region.handler.begin.value ||
          region.handler.end.value == flow.instructions.size())
        return fail(RegionError::kExceptionShape);
      const auto& instruction = flow.instructions[region.body.end.value];
      const auto* jump = std::get_if<JumpOperation>(&instruction.operation);
      if (!jump || jump->target != flow.instructions[region.handler.end.value].pc ||
          instruction.effects.normal != LanguageEffect::kNone ||
          instruction.effects.before_throw != LanguageEffect::kNone ||
          instruction.effects.exceptional != ExceptionalCompletion::kImpossible ||
          gaps[block.end.value]) return fail(RegionError::kExceptionShape, instruction.pc);
      gaps[block.end.value] = TryId{static_cast<std::uint32_t>(i)};
    }
    // A gap label lives inside its try body. No outside edge may use it.
    for (std::size_t i = 0; i < count; ++i)
      for (std::size_t e = 0; e < normal.blocks[i].successor_count; ++e) {
        const auto target = normal.blocks[i].successors[e].target.value;
        if (gaps[target]) {
          const auto& region = blocks[gaps[target]->value];
          if (i < region.begin.value || i >= region.end.value)
            return fail(RegionError::kExceptionShape, normal.blocks[i].end);
        }
      }
    std::size_t next = 0;
    const auto sequence = [&](auto&& self, std::uint32_t first,
                              std::uint32_t last) -> RegionNode {
      if (first >= last) throw fail(RegionError::kExceptionShape);
      std::optional<RegionNode> root;
      for (auto i = first; i < last;) {
        const auto start = i;
        RegionNode node{RegionBlock{BlockId{i}}};
        if (next < regions.source_order.size() &&
            regions.source_order[next].value >= blocks.size())
          throw fail(RegionError::kExceptionShape);
        if (next < regions.source_order.size() &&
            blocks[regions.source_order[next].value].begin.value == i) {
          const auto id = regions.source_order[next++];
          const auto& region = blocks[id.value];
          if (region.handler_end.value > last) throw fail(RegionError::kExceptionShape);
          auto body = self(self, region.begin.value, region.end.value);
          std::optional<InstructionId> skip;
          if (region.end != region.handler_begin) {
            body = RegionNode{RegionLabel{region.end, std::make_unique<RegionNode>(std::move(body))}};
            skip = regions.regions[id.value].body.end;
          }
          auto handler = self(self, region.handler_begin.value, region.handler_end.value);
          node = RegionNode{RegionTry{id, std::make_unique<RegionNode>(std::move(body)),
              std::make_unique<RegionNode>(std::move(handler)), skip}};
          i = region.handler_end.value;
        } else {
          ++i;
        }
        if (!root) root = std::move(node);
        else {
          RegionSequence joined;
          joined.children.reserve(2);
          joined.children.push_back({RegionLabel{BlockId{start},
              std::make_unique<RegionNode>(std::move(*root))}});
          joined.children.push_back(std::move(node));
          root = RegionNode{std::move(joined)};
        }
      }
      return std::move(*root);
    };
    SemanticRegionTree tree;
    tree.order_positions.resize(count);
    if (tree.order_positions.capacity() > count) return fail(RegionError::kResourceLimit);
    for (std::uint32_t i = 0; i < count; ++i) tree.order_positions[i] = i;
    tree.root = sequence(sequence, 0, static_cast<std::uint32_t>(count));
    if (next != catches) return fail(RegionError::kExceptionShape);
    tree.catches = catches;
    tree.allocated_bytes = tree_bytes;
    return tree;
  } catch (const RegionFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return fail(RegionError::kResourceLimit);
  } catch (const std::length_error&) { return fail(RegionError::kResourceLimit); }
}
}  // namespace abcd::analysis
