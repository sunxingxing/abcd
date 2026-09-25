#ifndef ABCD_SOURCE_SEMANTIC_ENTRY_H_
#define ABCD_SOURCE_SEMANTIC_ENTRY_H_

#include "abcd/source/semantic_body.h"

namespace abcd::source {
// Owner supplies the unchanged body/IR/frame and validates record identity,
// profile and module ownership. This shared pipeline proves local captures,
// scopes and source effects; it does not certify publication or equivalence.
// budget includes the already-live IR, but excludes caller-owned metadata,
// module/export context and body. Printing uses a separate output allowance;
// work bounds retain the existing per-stage contract, not an aggregate quota.
// Record exports are private mutable state:
// discard them after any failed attempt, never retry with a partially used plan.
[[nodiscard]] SemanticBodyResult BuildEntrySource(
    const abc::MethodBody& body, const analysis::SemanticMethod& method,
    const abc::FrameLayout& frame, SourceForm form, std::string_view name,
    const MethodConstants& constants, ModuleExports* exports,
    analysis::SemanticBudget budget, std::size_t output_bytes,
    RoutePolicy policy = RoutePolicy::kLegacyAllowed);
}  // namespace abcd::source
#endif
