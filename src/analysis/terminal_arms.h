#ifndef ABCD_ANALYSIS_TERMINAL_ARMS_H_
#define ABCD_ANALYSIS_TERMINAL_ARMS_H_

#include "region_if.h"

namespace abcd::analysis {
// Input is a complete acyclic topological order. Caller admits one output
// BlockId per block and zeroed marks. Only a unique-predecessor terminal arm
// can move: its header already dominates it and there are no outgoing edges.
inline void OrderTerminalArms(const NormalFlow& flow,
    std::span<const EntryCount> entries, std::span<const BlockId> order,
    std::span<std::uint32_t> marks, std::vector<BlockId>& output) {
  for (const auto id : order) {
    if (marks[id.value]) continue;
    output.push_back(id);
    marks[id.value] = 1;
    const auto& header = flow.blocks[id.value];
    if (header.transfer != abc::ControlTransfer::kBranch ||
        header.successor_count != kMaxNormalSuccessors) continue;
    for (std::size_t e = 0; e < header.successor_count; ++e) {
      const auto& edge = header.successors[e];
      if (edge.kind != FlowEdgeKind::kFallthrough || marks[edge.target.value] ||
          entries[edge.target.value] != EntryCount::kSingle) continue;
      const auto& arm = flow.blocks[edge.target.value];
      if (arm.successor_count ||
          (arm.transfer != abc::ControlTransfer::kReturn &&
           arm.transfer != abc::ControlTransfer::kThrow)) continue;
      output.push_back(edge.target);
      marks[edge.target.value] = 1;
    }
  }
}
}  // namespace abcd::analysis
#endif
