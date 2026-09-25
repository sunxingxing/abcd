#ifndef ABCD_ANALYSIS_EXCEPTION_CALLS_H_
#define ABCD_ANALYSIS_EXCEPTION_CALLS_H_

#include "abcd/analysis/instruction_ssa.h"

namespace abcd::analysis {
enum class ReceiverEvaluation : std::uint8_t { kFuse, kPreserveOrder };
enum class ReceiverRegion : std::uint8_t { kLinear, kForward };
struct ExceptionCallRoute {
  InstructionId property;
  ReceiverEvaluation evaluation;
  ReceiverRegion region;
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
// Same immutable BuildExceptionFlow result and matching phased SSA. Accepts direct read/call and exact
// read/spill/reload/call intervals, with single-entry interiors and equal
// dispatch/Catch gateways. Longer intervals require a single dispatch and
// Catch gateway across all throwing evaluations. Effectful interiors and any
// longer local-catch interval require kPreserveOrder emission; they are never
// permission to delay the property read. This is NOT full call
// emission permission: AST must
// prove receiver identity and single uses of all removed SSA definitions,
// including phi inputs, so no handler observes an eliminated register write.
// kForward additionally requires the AST's forward argument-region proof and
// is only issued for graphs without Catch nodes. It is not a control-flow
// certificate by itself. Candidates follow at most two SSA slot copies.
// O(I+N+E+R) work (R: call reads), bounded O(I+N) storage; prefix interval queries.
[[nodiscard]] std::variant<ExceptionCalls, ExceptionCallError>
ProveExceptionCalls(const ExceptionFlow& flow, const InstructionSsa& ssa,
                    SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
