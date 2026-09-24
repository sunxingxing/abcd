#ifndef ABCD_ANALYSIS_EXCEPTION_LOCALS_H_
#define ABCD_ANALYSIS_EXCEPTION_LOCALS_H_

#include "abcd/analysis/exception_environment.h"
#include "abcd/analysis/exception_value_flow.h"
#include "abcd/analysis/semantic_validation.h"

namespace abcd::analysis {
struct ExceptionLocals {
  LocalAccessPlan plan;  // Original instruction coordinates for the AST.
  std::vector<ExceptionLoadFact> loads;  // Normal Commit value premises.
  std::size_t AllocatedBytes() const {
    return plan.AllocatedBytes() + loads.capacity() * sizeof(ExceptionLoadFact);
  }
};
enum class ExceptionLocalError : std::uint8_t {
  kInvalidEnvironment, kUninitialized, kCapturedEnvironment, kResourceLimit
};
struct ExceptionLocalFailure {
  ExceptionLocalError kind;
  abc::CodeOffset pc;
};

// Same unchanged flow/environment analysis. Conditional proof: after applying
// loads to value propagation, EVERY store must pass VerifyExceptionInputs.
// Allocation dominates a completed store, whose subtree covers each load.
// Capture-capable creation marks its current local environment and ancestors
// escaped; their loads require a future closure-write proof, not this premise.
// No normal-only dominance, default undefined, or branch-union initialization.
// Returned plan has no normal-CFG capture-initialization certificate.
// O((N+E)log N + L log L + I + A) time; bounded O(N+E+I+A+L) space,
// for phase nodes/edges, lexical accesses, instructions and environments.
[[nodiscard]] std::variant<ExceptionLocals, ExceptionLocalFailure>
ProveExceptionLocals(const ExceptionFlow& flow, const EnvironmentFlow& environments,
                     SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
