#ifndef ABCD_ANALYSIS_SEMANTIC_LOCALS_H_
#define ABCD_ANALYSIS_SEMANTIC_LOCALS_H_

#include "abcd/analysis/semantic_validation.h"

namespace abcd::analysis {
// Conditional initialization proof: all stores must subsequently pass the
// register SSA Language check. A forward CFG requires allocation dominance
// of the initializing store, then store dominance of later reads/captures.
// Cyclic graphs retain the stricter same-block allocation/store/read proof.
// Function creation since allocation blocks reads unless the owner promises
// Language-only transitive closure stores. Initialization remains mandatory;
// this premise says nothing about value identity. No phi-cell promotion.
std::variant<LocalAccessPlan, SemanticProofFailure> ProveLocalAccesses(
    const SemanticMethod& method, const EnvironmentFlow& environments,
    SemanticBudget budget, ClosureWrites closure_writes);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_SEMANTIC_LOCALS_H_
