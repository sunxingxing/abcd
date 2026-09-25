#ifndef ABCD_ANALYSIS_LOOP_INTERVALS_H_
#define ABCD_ANALYSIS_LOOP_INTERVALS_H_

#include <optional>
#include <span>
#include <vector>

#include "abcd/analysis/semantic_region.h"

namespace abcd::analysis {
// Caller admits these three block-sized buffers before allocation. Endpoints
// are inclusive. The sized ends/enclosing arrays must contain nullopt and
// the reserved stack must be empty on entry.
// O(B+E): certify laminar backward-edge intervals and normal single entry.
// Reachability and exception-scope compatibility are separate owner proofs.
[[nodiscard]] std::optional<RegionFailure> FindLoopIntervals(
    const NormalFlow& flow,
    std::span<std::optional<std::uint32_t>> ends,
    std::span<std::optional<std::uint32_t>> enclosing,
    std::vector<std::uint32_t>& stack);
}  // namespace abcd::analysis
#endif
