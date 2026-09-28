#ifndef ABCD_ANALYSIS_AWAIT_CATCH_H_
#define ABCD_ANALYSIS_AWAIT_CATCH_H_

#include "abcd/analysis/semantic_ir.h"

namespace abcd::analysis {
enum class AwaitCatchError { kRegion, kObservedRegister, kResourceLimit };
struct AwaitCatchFailure {
  AwaitCatchError kind;
  abc::CodeOffset pc;
};
struct AwaitCatchProof {
  std::uint64_t work_units = 0;
};
// Requires unchanged decoded input, FindAwaitRegions certificates, and a proven
// final compiler completion handler. Result/mode registers must be overwritten
// before a handler can read them, or remain unobserved until exit. A straight
// prefix proves overwrite/exit; a suffix containing all its branch/exception
// targets proves deadness when it has no reads of the slot at all. Unknown
// effects and targets escaping that suffix cannot authorize this second case.
// Exact certified await protocol sites contribute their typed coroutine reads;
// arbitrary raw coroutine instructions remain unknown, not exits or no-ops.
// Protocol-local result/mode reads observe new definitions. A continuation with
// an external branch/handler entry is not local and still observes its input.
// Shared compressed range-assignment index: O((I+A+H) log(A+1) + I log(R+1))
// after dispatch construction, R dispatch spans, H handlers; O(A) extra storage excluding
// dispatch. No per-await walk through the handler body.
[[nodiscard]] std::variant<AwaitCatchProof, AwaitCatchFailure>
ProveAwaitCatchInputs(const abc::MethodBody& body,
                     std::span<const AwaitRegion> regions,
                     abc::ProfileId profile, std::uint64_t frame_size,
                     abc::CodeOffset completion_handler,
                     SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
