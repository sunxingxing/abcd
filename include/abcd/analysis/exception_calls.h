#ifndef ABCD_ANALYSIS_EXCEPTION_CALLS_H_
#define ABCD_ANALYSIS_EXCEPTION_CALLS_H_

#include "abcd/analysis/exception_flow.h"

namespace abcd::analysis {
struct ExceptionCalls {
  // Indexed by original call instruction; absent means no route certificate.
  std::vector<std::optional<InstructionId>> properties;
  std::uint64_t work_units = 0;
  std::size_t AllocatedBytes() const {
    return properties.capacity() * sizeof(std::optional<InstructionId>);
  }
};
enum class ExceptionCallError : std::uint8_t { kInvalidFlow, kResourceLimit };
// Same immutable BuildExceptionFlow result. Accepts direct read/call and exact
// read/spill/reload/call intervals, with single-entry interiors and equal
// dispatch/Catch gateways; longer private scalar/string argument intervals
// require both exceptions to propagate to the caller. This is NOT full call
// fusion permission: AST must
// prove receiver identity and single uses of all removed SSA definitions,
// including phi inputs, so no handler observes an eliminated register write.
// O(I+N+E) work and bounded O(I+N) storage; prefix queries bound long intervals.
[[nodiscard]] std::variant<ExceptionCalls, ExceptionCallError>
ProveExceptionCalls(const ExceptionFlow& flow, SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
