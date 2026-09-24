#include "argument_region.h"
#include "semantic_ast_input.h"

#include <algorithm>

namespace abcd::source {
std::optional<SourceArgumentRegion> ProveArgumentRegion(
    const AstMethod& method, std::size_t property,
    std::size_t call) {
  if (property >= call || call >= method.instructions.size()) return {};
  const auto& blocks = method.flow.blocks;
  const auto block_of = [&](std::size_t instruction) {
    return std::upper_bound(blocks.begin(), blocks.end(), instruction,
        [](auto target, const auto& block) {
          return target < block.first_instruction;
        });
  };
  const auto first_end = block_of(property);
  const auto last_end = block_of(call);
  if (first_end == blocks.begin() || last_end == blocks.begin()) return {};
  const auto first = static_cast<std::size_t>(first_end - blocks.begin() - 1);
  const auto last = static_cast<std::size_t>(last_end - blocks.begin() - 1);
  if (first >= last) return {};
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    const auto& block = blocks[i];
    if (i >= first && i < last && !block.successor_count) return {};
    for (std::size_t edge = 0; edge < block.successor_count; ++edge) {
      const auto target = block.successors[edge].target.value;
      if (i >= first && i < last) {
        // A finite forward-only interval cannot return, loop or bypass its
        // exit. Label emission therefore needs no synthetic return semantics.
        if (target <= i || target > last) return {};
      } else if (target > first && target <= last) {
        return {};  // Entry into the middle would skip the captured getter.
      }
    }
  }
  return SourceArgumentRegion{
      {static_cast<std::uint32_t>(property)},
      {static_cast<std::uint32_t>(call)},
      {static_cast<std::uint32_t>(first)},
      {static_cast<std::uint32_t>(last)}};
}
}  // namespace abcd::source
