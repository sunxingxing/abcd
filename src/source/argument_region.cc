#include "argument_region.h"
#include "semantic_ast_input.h"

#include <algorithm>

namespace abcd::source {
std::optional<analysis::RegionIf> ProveArgumentIf(
    const SourceAst& ast, const analysis::NormalFlow& flow) {
  if (!ast.argument_region) return {};
  const auto& argument = *ast.argument_region;
  constexpr std::uint32_t kDiamondArms = 2;
  constexpr auto kJoinDistance = kDiamondArms + 1;
  const auto first = argument.first_block.value;
  const auto last = argument.last_block.value;
  if (first >= last || last >= flow.blocks.size() ||
      last - first != kJoinDistance) return {};
  const analysis::BlockId body{first + 1};
  const analysis::BlockId alternate{body.value + 1};
  const auto& header = flow.blocks[first];
  if (header.transfer != abc::ControlTransfer::kBranch ||
      header.successor_count != analysis::kMaxNormalSuccessors) return {};
  bool taken = false;
  bool fallthrough = false;
  for (std::size_t edge = 0; edge < header.successor_count; ++edge) {
    const auto& successor = header.successors[edge];
    taken |= successor.kind == analysis::FlowEdgeKind::kTaken &&
             successor.target == alternate;
    fallthrough |= successor.kind == analysis::FlowEdgeKind::kFallthrough &&
                   successor.target == body;
  }
  const auto joins = [&](analysis::BlockId id) {
    const auto& arm = flow.blocks[id.value];
    return arm.successor_count == 1 &&
        (arm.transfer == abc::ControlTransfer::kJump ||
         arm.transfer == abc::ControlTransfer::kFallthrough) &&
        arm.successors[0].kind != analysis::FlowEdgeKind::kResume &&
        arm.successors[0].target == argument.last_block;
  };
  // ProveArgumentRegion already excludes external entries into either arm.
  // Both arms go directly to the join, so neither can enter the other arm.
  // This is the same single-entry diamond as RegionIf, independent of the
  // enclosing region tree's chosen topological ordering of those arms.
  if (!taken || !fallthrough || !joins(body) || !joins(alternate)) return {};
  return analysis::RegionIf{argument.first_block, body,
                             argument.last_block, alternate};
}

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
