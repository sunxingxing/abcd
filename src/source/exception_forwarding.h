#ifndef ABCD_SOURCE_EXCEPTION_FORWARDING_H_
#define ABCD_SOURCE_EXCEPTION_FORWARDING_H_
#include "for_in_plan.h"

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
// is consumed only by ForInPlan and does not rewrite existing handlers.
[[nodiscard]] std::variant<ExceptionForwarding, ForInFailure> AnalyzeExceptionForwarding(
    const analysis::ValueProgram&, analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
