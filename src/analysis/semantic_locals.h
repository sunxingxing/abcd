#ifndef ABCD_ANALYSIS_SEMANTIC_LOCALS_H_
#define ABCD_ANALYSIS_SEMANTIC_LOCALS_H_

#include "abcd/analysis/semantic_validation.h"

namespace abcd::analysis {
// Conditional initialization proof: all stores must subsequently pass the
// register SSA Language check. Allocation/store share one block; a forward
// CFG may additionally use dominance to cover subsequent reads and captures.
// any function creation since allocation blocks reads until capture effects
// are proven. No phi-cell promotion or unknown-call mutation assumption.
std::variant<LocalAccessPlan, SemanticProofFailure> ProveLocalAccesses(
    const SemanticMethod& method, const EnvironmentFlow& environments,
    SemanticBudget budget);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_SEMANTIC_LOCALS_H_
