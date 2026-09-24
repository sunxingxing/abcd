#ifndef ABCD_ANALYSIS_EXCEPTION_CAPTURES_H_
#define ABCD_ANALYSIS_EXCEPTION_CAPTURES_H_

#include "abcd/analysis/exception_environment.h"
#include "abcd/analysis/exception_value_flow.h"
#include "abcd/analysis/semantic_validation.h"

namespace abcd::analysis {
struct ExceptionCaptures {
  std::vector<CapturedReadFact> reads;
  std::vector<ExceptionLoadFact> loads;
  std::uint64_t work_units = 0;
  std::size_t AllocatedBytes() const {
    return reads.capacity() * sizeof(CapturedReadFact) + loads.capacity() * sizeof(ExceptionLoadFact);
  }
};
enum class ExceptionCaptureError : std::uint8_t { kInvalidEnvironment, kResourceLimit };
// PARAMETRIC template only: parent must rebase and prove every emitted capture
// is an initialized Language cell before publishing. Local environments are
// never granted this premise. The unchanged flow/environment owners must match.
// Reads use original instruction IDs; load premises use reachable Commit IDs.
// O(I+N+L) time and bounded O(I+L) space for instructions, nodes and bindings.
std::variant<ExceptionCaptures, ExceptionCaptureError> PlanExceptionCaptures(
    const ExceptionFlow& flow, const EnvironmentFlow& environments,
    SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
