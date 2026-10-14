#ifndef ABCD_ANALYSIS_AWAIT_CATCH_H_
#define ABCD_ANALYSIS_AWAIT_CATCH_H_

#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/yield_region.h"

namespace abcd::analysis {
enum class AwaitCatchError { kRegion, kObservedRegister, kResourceLimit };
enum class CatchInputPrefix : std::uint8_t { kUnprovedPaths, kRead };
struct AwaitCatchObservation {
  abc::CodeOffset handler;
  abc::RegisterId slot;
  CatchInputPrefix prefix;
  bool suffix_known, suffix_confined;
  bool operator==(const AwaitCatchObservation&) const = default;
};
struct AwaitCatchFailure {
  AwaitCatchError kind;
  std::optional<abc::CodeOffset> pc;
  std::optional<AwaitCatchObservation> observation;
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
// Optional implicit_exit is the owner's exact async-generator implicit return
// plus outer rejection wrapper. Neither observes incoming local temporaries.
[[nodiscard]] std::variant<AwaitCatchProof, AwaitCatchFailure>
ProveAwaitCatchInputs(const abc::MethodBody& body,
                     std::span<const AwaitRegion> regions,
                     abc::ProfileId profile, std::uint64_t frame_size,
                     abc::CodeOffset completion_handler,
                     SemanticBudget budget = {},
                     std::optional<abc::CodeOffset> implicit_exit = std::nullopt);
// Same catch-input analysis for exact single-entry synchronous/async yields.
// Native yield throws before committing result/mode, whereas the VM's THROW
// route commits both. Admit that projection only when a selected user handler
// cannot observe either incoming temporary; keep its other frame values intact.
// Optional implicit_exit starts the owner's proved async-generator undefined
// return/settlement tail. Its recycled VM slots are not source reads. Never
// supply an explicit source return or unverified settlement as this boundary.
// Shared awaits are original single-entry certificates checked by that owner;
// their private reads do not observe earlier yield temporaries.
[[nodiscard]] std::variant<AwaitCatchProof, AwaitCatchFailure>
ProveYieldCatchInputs(const abc::MethodBody& body,
                     std::span<const YieldSite> regions,
                     abc::ProfileId profile, std::uint64_t frame_size,
                     abc::CodeOffset completion_handler,
                     SemanticBudget budget = {},
                     std::optional<abc::CodeOffset> implicit_exit = std::nullopt,
                     std::span<const AwaitRegion> awaits = {});
}  // namespace abcd::analysis
#endif
