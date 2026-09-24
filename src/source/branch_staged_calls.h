#ifndef ABCD_SOURCE_BRANCH_STAGED_CALLS_H_
#define ABCD_SOURCE_BRANCH_STAGED_CALLS_H_

#include "abcd/analysis/named_invocation.h"
#include "abcd/analysis/normal_flow.h"
#include "abcd/source/linear_body.h"

namespace abcd::source {
struct BranchStagedCalls {
  std::vector<analysis::StagedNamedInvocation> calls;
  [[nodiscard]] std::size_t AllocatedBytes() const {
    return calls.capacity() * sizeof(analysis::StagedNamedInvocation);
  }
};
// Match within unchanged basic blocks, then prove the spilled callee's SSA
// value has exactly one use: the reload. This includes successor blocks and
// phi edges, so source emission may discard the private spill safely.
[[nodiscard]] std::variant<BranchStagedCalls, BodyEmitFailure>
BuildBranchStagedCalls(const abc::MethodBody& body,
                       const analysis::NormalFlow& graph,
                       std::uint64_t frame_size, std::size_t bytes,
                       std::uint64_t work);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_BRANCH_STAGED_CALLS_H_
