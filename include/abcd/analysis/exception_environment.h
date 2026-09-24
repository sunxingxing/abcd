#ifndef ABCD_ANALYSIS_EXCEPTION_ENVIRONMENT_H_
#define ABCD_ANALYSIS_EXCEPTION_ENVIRONMENT_H_

#include "abcd/analysis/environment_flow.h"
#include "abcd/analysis/exception_flow.h"

namespace abcd::analysis {
// Requires unchanged BuildExceptionFlow output. Result indices refer to phase
// nodes, not original instructions: allocations occur at Commit, accesses at
// Before. Catch preserves the throwing frame's environment identity. This is
// topology/binding proof only, not lexical cell values or source admission.
// O(N+E+(P+L)log D) time and bounded storage including the site/graph adapter.
// Unequal environment identities at joins remain an explicit refusal.
[[nodiscard]] std::variant<EnvironmentFlow, EnvironmentFlowFailure>
AnalyzeExceptionEnvironments(const ExceptionFlow& flow,
                             EnvironmentBudget budget = {});
}  // namespace abcd::analysis
#endif
