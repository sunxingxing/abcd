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
// before a handler can read them, or the path must exit. Branches and further
// local exceptional transfers conservatively stop this straight-prefix proof.
// Shared compressed range-assignment index: O((I+A) log(A+1) + I log(R+1))
// after dispatch construction, R dispatch spans; O(A) extra storage excluding
// dispatch. No per-await walk through the handler body.
[[nodiscard]] std::variant<AwaitCatchProof, AwaitCatchFailure>
ProveAwaitCatchInputs(const abc::MethodBody& body,
                     std::span<const AwaitRegion> regions,
                     abc::ProfileId profile, std::uint64_t frame_size,
                     abc::CodeOffset completion_handler,
                     SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
