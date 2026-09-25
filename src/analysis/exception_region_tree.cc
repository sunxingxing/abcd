#include "abcd/analysis/semantic_region.h"

#include <new>
#include <stdexcept>

#include "loop_intervals.h"
#include "region_if.h"

namespace abcd::analysis {
namespace {
struct CatchBlocks {
  BlockId begin;
  BlockId end;
  BlockId handler_begin;
  BlockId handler_end;
};
bool SameScope(const std::optional<CatchScope>& left,
               const std::optional<CatchScope>& right) {
  if (!left || !right) return left.has_value() == right.has_value();
  return left->region == right->region && left->part == right->part;
}
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
  constexpr std::size_t kNodesPerBlock = 6;
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
  available -= count * sizeof(std::optional<TryId>);
  constexpr auto kLoopScratchPerBlock =
      2 * sizeof(std::optional<std::uint32_t>) + sizeof(std::uint32_t) +
      sizeof(std::size_t) + sizeof(EntryCount);
  if (count > available / kLoopScratchPerBlock)
    return fail(RegionError::kResourceLimit);
  try {
    std::vector<std::optional<BlockId>> boundaries(map_count);
    std::vector<CatchBlocks> blocks(catches);
    std::vector<std::optional<TryId>> gaps(count);
    std::vector<std::optional<std::uint32_t>> ends(count), enclosing(count);
    std::vector<std::uint32_t> stack;
    stack.reserve(count);
    std::vector<std::size_t> scope_runs(count);
    std::vector<EntryCount> entries(count);
    if (boundaries.capacity() > map_count || blocks.capacity() > catches || gaps.capacity() > count ||
        ends.capacity() > count || enclosing.capacity() > count ||
        stack.capacity() > count || scope_runs.capacity() > count || entries.capacity() > count)
      return fail(RegionError::kResourceLimit);
    if (const auto failure = CountRegionEntries(normal, entries)) return *failure;
    if (const auto failure = FindLoopIntervals(normal, ends, enclosing, stack))
      return *failure;
    std::size_t covered = 0;
    std::size_t scope_run = 0;
    std::size_t loops = 0;
    for (std::size_t i = 0; i < count; ++i) {
      const auto& block = normal.blocks[i];
      if (block.first_instruction != covered || !block.instruction_count ||
          block.instruction_count > flow.instructions.size() - covered ||
          block.successor_count > block.successors.size())
        return fail(RegionError::kExceptionShape, block.begin);
      boundaries[covered] = BlockId{static_cast<std::uint32_t>(i)};
      if (covered && !SameScope(regions.scopes[covered - 1], regions.scopes[covered]))
        ++scope_run;
      scope_runs[i] = scope_run;
      for (auto instruction = covered + 1;
           instruction < covered + block.instruction_count; ++instruction)
        if (!SameScope(regions.scopes[covered], regions.scopes[instruction]))
          return fail(RegionError::kExceptionShape, block.begin);
      covered += block.instruction_count;
      if (ends[i]) ++loops;
    }
    if (covered != flow.instructions.size()) return fail(RegionError::kExceptionShape);
    if (loops > kMaxSemanticRegionDepth - count - 2 * catches)
      return fail(RegionError::kDepth);
    // A constant scope run proves the entire loop stays in one protected body
    // or handler, not just that its endpoints share a scope. Exceptional exits
    // retain the enclosing catch; crossing or enclosing catches stays refused.
    for (std::size_t i = 0; i < count; ++i)
      if (ends[i] && scope_runs[i] != scope_runs[*ends[i]])
        return fail(RegionError::kExceptionShape, normal.blocks[i].begin);
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
                              std::uint32_t last, bool loop_body) -> RegionNode {
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
          auto body = self(self, region.begin.value, region.end.value, false);
          std::optional<InstructionId> skip;
          if (region.end != region.handler_begin) {
            body = RegionNode{RegionLabel{region.end, std::make_unique<RegionNode>(std::move(body))}};
            skip = regions.regions[id.value].body.end;
          }
          auto handler = self(self, region.handler_begin.value, region.handler_end.value, false);
          node = RegionNode{RegionTry{id, std::make_unique<RegionNode>(std::move(body)),
              std::make_unique<RegionNode>(std::move(handler)), skip}};
          i = region.handler_end.value;
        } else if (ends[i] && !(loop_body && i == first)) {
          if (*ends[i] >= last) throw fail(RegionError::kExceptionShape);
          node = RegionNode{RegionLoop{BlockId{i},
              std::make_unique<RegionNode>(self(self, i, *ends[i] + 1, true))}};
          i = *ends[i] + 1;
        } else if (i + kIfBlocks < last && scope_runs[i] == scope_runs[i + 1] &&
                   CanFormIf(normal, entries, BlockId{i}, BlockId{i + 1},
                             BlockId{i + kIfBlocks})) {
          node = RegionNode{RegionIf{BlockId{i}, BlockId{i + 1}, BlockId{i + kIfBlocks}}};
          i += kIfBlocks;
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
    tree.root = sequence(sequence, 0, static_cast<std::uint32_t>(count), false);
    if (next != catches) return fail(RegionError::kExceptionShape);
    tree.catches = catches;
    tree.allocated_bytes = tree_bytes;
    return tree;
  } catch (const RegionFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return fail(RegionError::kResourceLimit);
  } catch (const std::length_error&) { return fail(RegionError::kResourceLimit); }
}
}  // namespace abcd::analysis
