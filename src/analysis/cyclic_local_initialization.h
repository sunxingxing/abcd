#ifndef ABCD_ANALYSIS_CYCLIC_LOCAL_INITIALIZATION_H_
#define ABCD_ANALYSIS_CYCLIC_LOCAL_INITIALIZATION_H_
#include "abcd/analysis/semantic_validation.h"
namespace abcd::analysis {
// A sufficient activation proof, not a source-layout rule. A dominates S and
// S dominates R, with strict instruction order inside one block, imply that
// every path from the most recent A to R executes S. A back edge through A
// cannot bypass S; static dominance would then be false. All stores still
// require Language inputs in VerifySemanticSource. Transitive closure writes
// are separately required to preserve that domain. No value forwarding.
std::variant<std::uint64_t, SemanticProofFailure> ProveCyclicLocalInitialization(
    const SemanticMethod&, LocalAccessPlan&, std::span<const std::optional<InstructionId>> allocations,
    SemanticBudget, ClosureWrites);
}
#endif
