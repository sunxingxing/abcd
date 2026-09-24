#ifndef ABCD_ANALYSIS_GUARDED_GLOBAL_CALL_H_
#define ABCD_ANALYSIS_GUARDED_GLOBAL_CALL_H_

#include <cstddef>
#include <optional>

#include "abcd/abc/method_body.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/normal_flow.h"

namespace abcd::analysis {
struct GuardedGlobalCall {
  std::size_t first;
  std::size_t after;
  std::size_t function;
  abc::PoolIndex key;
  abc::PoolIndex receiver_global;
  abc::PoolIndex receiver_property;
  abc::PoolIndex call_global;
  abc::PoolIndex call_property;
};

// Recognizes the v7 compiler's guarded global method call, independently of
// names. The condition is !(key in receiver_global[receiver_property]); the
// taken body calls call_global[call_property] with receiver call_global and
// (receiver_global[receiver_property], key, fresh DEFINEFUNC) arguments.
// Requires exact decoded operands, branch edges, dead transient registers and
// a join instruction that overwrites (without reading) the merged accumulator.
// This is evidence for later source admission, not permission to omit effects.
// Forward normal CFG only; loops and resume edges remain unsupported.
// O(I log I) time (CFG target lookup), O(I) budgeted auxiliary memory.
// monostate means no proof; allocation/CFG failures are not a non-match.
using GuardedCallResult =
    std::variant<std::monostate, GuardedGlobalCall, FlowFailure>;
[[nodiscard]] GuardedCallResult MatchGuardedGlobalCall(
    const abc::MethodBody& body, std::size_t first,
    std::uint64_t frame_size, FlowBudget budget = {});
}  // namespace abcd::analysis
#endif
