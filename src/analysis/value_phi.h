#ifndef ABCD_ANALYSIS_VALUE_PHI_H_
#define ABCD_ANALYSIS_VALUE_PHI_H_

#include "abcd/analysis/value_simplify.h"
#include <span>

namespace abcd::analysis {
struct PhiSimplifyStats {
  std::size_t replaced_uses = 0;
  std::size_t collapsed_values = 0;
  std::uint64_t work = 0;
};
// Read-only SSA identity facts. Iterative SCC collapse follows actual copy/phi
// equations; one external identity anchors a component, never a layout/name.
// Caller must first verify availability/uses. Source binding/scope and effect
// ownership remain separate; these facts do not authorize replaying producers.
struct ValueIdentities {
  std::vector<ValueId> canonical;
  std::size_t collapsed_values = 0;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
[[nodiscard]] std::variant<ValueIdentities, ValueSimplifyError> AnalyzeValueIdentities(
    const ValueProgram& graph, SemanticBudget budget = {});
// Candidate-only operation after copy canonicalization and input flow checking.
// Failure requires discarding the candidate. Caller checks resulting flow before
// committing. Budget covers additional scratch, not the already admitted graph.
[[nodiscard]] std::variant<PhiSimplifyStats, ValueSimplifyError> SimplifyPhis(
    ValueProgram& candidate, SemanticBudget budget,
    std::span<ValueId> representatives = {});
}  // namespace abcd::analysis
#endif
