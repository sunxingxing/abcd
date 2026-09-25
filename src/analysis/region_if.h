#ifndef ABCD_ANALYSIS_REGION_IF_H_
#define ABCD_ANALYSIS_REGION_IF_H_

#include <span>

#include "abcd/analysis/semantic_region.h"

namespace abcd::analysis {
enum class EntryCount : std::uint8_t { kNone, kSingle, kMultiple };
inline constexpr std::uint32_t kIfBlocks = 2;

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
// Constant-time SESE reduction. Owners prove ordering, reachability and that
// header/body belong to the same exception scope and lexical sequence.
inline bool CanFormIf(const NormalFlow& flow, std::span<const EntryCount> entries,
                     BlockId header, BlockId body, BlockId join) {
  const auto& head = flow.blocks[header.value];
  const auto& arm = flow.blocks[body.value];
  if (head.transfer != abc::ControlTransfer::kBranch ||
      head.successor_count != kMaxNormalSuccessors ||
      entries[body.value] != EntryCount::kSingle || arm.successor_count != 1 ||
      (arm.transfer != abc::ControlTransfer::kFallthrough &&
       arm.transfer != abc::ControlTransfer::kJump) ||
      arm.successors[0].target != join ||
      arm.successors[0].kind == FlowEdgeKind::kResume) return false;
  bool taken = false;
  bool fallthrough = false;
  for (std::size_t i = 0; i < head.successor_count; ++i) {
    const auto& edge = head.successors[i];
    taken |= edge.kind == FlowEdgeKind::kTaken && edge.target == join;
    fallthrough |= edge.kind == FlowEdgeKind::kFallthrough && edge.target == body;
  }
  return taken && fallthrough;
}
}  // namespace abcd::analysis
#endif
