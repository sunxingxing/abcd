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
// Candidate-only operation after copy canonicalization and input flow checking.
// Failure requires discarding the candidate. Caller checks resulting flow before
// committing. Budget covers additional scratch, not the already admitted graph.
[[nodiscard]] std::variant<PhiSimplifyStats, ValueSimplifyError> SimplifyPhis(
    ValueProgram& candidate, SemanticBudget budget,
    std::span<ValueId> representatives = {});
}  // namespace abcd::analysis
#endif
