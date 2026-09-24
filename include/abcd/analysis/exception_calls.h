#ifndef ABCD_ANALYSIS_EXCEPTION_CALLS_H_
#define ABCD_ANALYSIS_EXCEPTION_CALLS_H_

#include "abcd/analysis/exception_flow.h"

namespace abcd::analysis {
enum class ReceiverEvaluation : std::uint8_t { kFuse, kPreserveOrder };
struct ExceptionCallRoute {
  InstructionId property;
  ReceiverEvaluation evaluation;
};
struct ExceptionCalls {
  // Indexed by original call instruction; absent means no route certificate.
  std::vector<std::optional<ExceptionCallRoute>> routes;
  std::uint64_t work_units = 0;
  std::size_t AllocatedBytes() const {
    return routes.capacity() * sizeof(std::optional<ExceptionCallRoute>);
  }
};
enum class ExceptionCallError : std::uint8_t { kInvalidFlow, kResourceLimit };
// Same immutable BuildExceptionFlow result. Accepts direct read/call and exact
// read/spill/reload/call intervals, with single-entry interiors and equal
// dispatch/Catch gateways; longer private scalar/string argument intervals
// require both exceptions to propagate to the caller. Effectful interiors
// require caller-only exits at every site and kPreserveOrder emission; they
// are never permission to delay the property read. This is NOT full call
// emission permission: AST must
// prove receiver identity and single uses of all removed SSA definitions,
// including phi inputs, so no handler observes an eliminated register write.
// O(I+N+E) work and bounded O(I+N) storage; prefix queries bound long intervals.
[[nodiscard]] std::variant<ExceptionCalls, ExceptionCallError>
ProveExceptionCalls(const ExceptionFlow& flow, SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
