#ifndef ABCD_ANALYSIS_REGION_IF_H_
#define ABCD_ANALYSIS_REGION_IF_H_

#include <span>
#include <utility>

#include "abcd/analysis/semantic_region.h"

namespace abcd::analysis {
enum class EntryCount : std::uint8_t { kNone, kSingle, kMultiple };
inline constexpr std::uint32_t kIfBlocks = 2;
inline constexpr std::uint32_t kIfElseBlocks = kIfBlocks + 1;

// Caller supplies zero-initialized, admitted block storage. Saturation avoids
// overflow; region admission only distinguishes exactly one incoming edge.
inline std::optional<RegionFailure> CountRegionEntries(
    const NormalFlow& flow, std::span<EntryCount> entries) {
  if (entries.size() != flow.blocks.size())
    return RegionFailure{RegionError::kExceptionShape, {}};
  for (const auto& block : flow.blocks) {
    if (block.successor_count > block.successors.size())
      return RegionFailure{RegionError::kBackwardEdge, block.end};
    for (std::size_t i = 0; i < block.successor_count; ++i) {
      const auto target = block.successors[i].target.value;
      if (target >= entries.size())
        return RegionFailure{RegionError::kBackwardEdge, block.end};
      entries[target] = entries[target] == EntryCount::kNone
          ? EntryCount::kSingle : EntryCount::kMultiple;
    }
  }
  return {};
}
inline bool IsIfArm(const NormalFlow& flow, std::span<const EntryCount> entries,
                    BlockId body, BlockId join) {
  const auto& arm = flow.blocks[body.value];
  if (entries[body.value] != EntryCount::kSingle) return false;
  if (arm.transfer == abc::ControlTransfer::kReturn ||
      arm.transfer == abc::ControlTransfer::kThrow)
    return arm.successor_count == 0;
  return arm.successor_count == 1 &&
      (arm.transfer == abc::ControlTransfer::kFallthrough ||
       arm.transfer == abc::ControlTransfer::kJump) &&
      arm.successors[0].target == join &&
      arm.successors[0].kind != FlowEdgeKind::kResume;
}
// Constant-time structural reduction. Owners prove ordering, reachability and that
// header/body belong to the same exception scope and lexical sequence.
inline bool CanFormIf(const NormalFlow& flow, std::span<const EntryCount> entries,
                     BlockId header, BlockId body, BlockId join,
                     std::optional<BlockId> alternate = {}) {
  const auto& head = flow.blocks[header.value];
  if (head.transfer != abc::ControlTransfer::kBranch ||
      head.successor_count != kMaxNormalSuccessors ||
      !IsIfArm(flow, entries, body, join) ||
      (alternate && !IsIfArm(flow, entries, *alternate, join))) return false;
  bool taken = false;
  bool fallthrough = false;
  for (std::size_t i = 0; i < head.successor_count; ++i) {
    const auto& edge = head.successors[i];
    taken |= edge.kind == FlowEdgeKind::kTaken && edge.target == alternate.value_or(join);
    fallthrough |= edge.kind == FlowEdgeKind::kFallthrough && edge.target == body;
  }
  return taken && fallthrough;
}
// Kahn's ready queue may discover the taken arm first. These two adjacent
// single-entry arms have no edge between them: each terminates or reaches
// the same forward join. Normalize only this certified local diamond, leaving
// shared graph ordering and all unrelated blocks untouched.
inline void OrderIfArms(const NormalFlow& flow,
                       std::span<const EntryCount> entries,
                       std::span<BlockId> order) {
  for (std::size_t i = 0; i + kIfElseBlocks < order.size(); ++i) {
    if (CanFormIf(flow, entries, order[i], order[i + kIfBlocks],
                  order[i + kIfElseBlocks], order[i + 1]))
      std::swap(order[i + 1], order[i + kIfBlocks]);
  }
}
}  // namespace abcd::analysis
#endif
