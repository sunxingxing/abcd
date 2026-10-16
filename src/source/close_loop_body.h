#ifndef ABCD_SOURCE_CLOSE_LOOP_BODY_H_
#define ABCD_SOURCE_CLOSE_LOOP_BODY_H_
#include "close_completion.h"
#include "abcd/analysis/dominators.h"
namespace abcd::source {
// Borrowed, method-owned indexes. The owner holds their charge and lifetime;
// verification constructs them again from the original flow.
struct CloseBodyIndex {
  const analysis::ExceptionFlow* flow;
  std::span<const std::optional<analysis::ExceptionNodeId>> before, commit;
  const analysis::DominatorTree* dominance;
};
// Original control only; source ranges are checked AFTER the body is derived.
// Return protection stays close code; only the body wrapper receives this role.
[[nodiscard]] std::variant<CloseProtectedRange, CloseCompletionFailure> ProveCloseLoopBody(
    const analysis::ExceptionFlow&, const ExceptionPlan&, const DerivedClose&,
    analysis::TryId, analysis::SemanticBudget, bool reverse,
    const CloseBodyIndex* index = nullptr);
}
#endif
