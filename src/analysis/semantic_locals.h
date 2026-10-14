#ifndef ABCD_ANALYSIS_SEMANTIC_LOCALS_H_
#define ABCD_ANALYSIS_SEMANTIC_LOCALS_H_

#include "abcd/analysis/semantic_validation.h"

namespace abcd::analysis {
// Conditional initialization proof: all stores must subsequently pass the
// register SSA Language check. An acyclic CFG requires allocation dominance
// of the initializing store, then store dominance of later reads/captures.
// Physical backward edges do not establish cycles. An unclosed cyclic read
// uses the same allocation/store/read dominance proof without DAG rank facts.
// Unknown closure writers still refuse; all writes remain Language obligations.
// Each cell may have disjoint initializing dominator regions. Original site
// numbers order instructions only inside one block, never across blocks.
// Function creation since allocation blocks reads unless the owner promises
// Language-only transitive closure stores. Initialization remains mandatory;
// this premise says nothing about value identity. No phi-cell promotion.
// Separately records fresh allocation-to-store prefixes containing only scalar
// moves, function creations and stores into that environment. These permit
// capture before initialization, never an early local read or external escape.
std::variant<LocalAccessPlan, SemanticProofFailure> ProveLocalAccesses(
    const SemanticMethod& method, const EnvironmentFlow& environments,
    SemanticBudget budget, ClosureWrites closure_writes,
    std::span<const CapturePreservation> preserved);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_SEMANTIC_LOCALS_H_
