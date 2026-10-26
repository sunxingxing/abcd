#ifndef ABCD_SOURCE_GUARDED_CATCH_VERIFICATION_H_
#define ABCD_SOURCE_GUARDED_CATCH_VERIFICATION_H_
#include "abcd/analysis/exception_throw_domain.h"
#include "tree_checks.h"

namespace abcd::source {
inline void VerifyGuardedCatchInput(const std::shared_ptr<const analysis::GuardedCatchEvidence>& input,
                                   TreeCheckBudget& budget) {
  if (!input) return;
  try { budget.Step(analysis::VerifyGuardedCatchEvidence(*input, budget.Remaining())); }
  catch (const analysis::ExceptionThrowDomainFailure& failure) {
    throw failure.kind == analysis::ExceptionThrowDomainError::kResourceLimit ? TreeStructureError::kResourceLimit :
        failure.kind == analysis::ExceptionThrowDomainError::kWorkLimit ? TreeStructureError::kWorkLimit : TreeStructureError::kOwnership;
  } catch (const analysis::RawEventFailure& failure) {
    throw failure.kind == analysis::RawEventError::kResourceLimit ? TreeStructureError::kResourceLimit :
        failure.kind == analysis::RawEventError::kWorkLimit ? TreeStructureError::kWorkLimit : TreeStructureError::kOwnership;
  }
}
}  // namespace abcd::source
#endif
