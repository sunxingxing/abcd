#ifndef ABCD_SOURCE_EXCEPTION_FORWARDING_H_
#define ABCD_SOURCE_EXCEPTION_FORWARDING_H_
#include "for_in_plan.h"
#include "exception_plan.h"
#include <span>

namespace abcd::source {
struct ForwardedException {
  analysis::ValueId payload;
  std::optional<analysis::ValueHandler> destination;
  bool operator==(const ForwardedException&) const = default;
};
struct ExceptionForwarding {
  // Universal normal-path summaries. An absent entry includes nonterminating
  // paths, changed payload/dispatch, observable effects and ordinary returns.
  std::vector<std::optional<ForwardedException>> blocks;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Requires verified availability, use chains, effects and original-flow
// correspondence; this query is not an input-graph verifier.
// Original SSA identities and exceptional exits are authoritative. Pure
// branches/joins are allowed; all paths must terminate by rethrowing the same
// value to the same destination. No instruction-sequence matching. This query
// is consumed only by verified enumeration projections and does not rewrite
// ordinary handlers in already-complete methods.
[[nodiscard]] std::variant<ExceptionForwarding, ForInFailure> AnalyzeExceptionForwarding(
    const analysis::ValueProgram&, analysis::SemanticBudget = {});
// A forwarding region owns only its handler paths, never protected user ops.
// Every original incoming exception is indexed separately; no physical Catch
// or iterator event is invented for a transparent path.
struct ProtocolForwarding {
  std::vector<std::optional<ForwardedException>> regions;
  std::vector<std::optional<analysis::TryId>> blocks, operations, exits;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  bool operator==(const ProtocolForwarding& b) const {
    return regions==b.regions && blocks==b.blocks && operations==b.operations && exits==b.exits;
  }
};
[[nodiscard]] std::variant<ProtocolForwarding, ForInFailure> AnalyzeProtocolForwarding(
    const analysis::ValueProgram&, const ExceptionPlan&,
    std::span<const std::uint8_t> owned_regions, std::span<const std::uint8_t> close_blocks,
    analysis::SemanticBudget = {}, std::span<const std::uint8_t> retained_regions = {},
    std::span<const std::uint8_t> entered_regions = {},
    std::span<const analysis::TryId> loop_bodies = {});
}  // namespace abcd::source
#endif
