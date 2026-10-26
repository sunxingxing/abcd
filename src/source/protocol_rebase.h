#ifndef ABCD_SOURCE_PROTOCOL_REBASE_H_
#define ABCD_SOURCE_PROTOCOL_REBASE_H_

#include "abcd/analysis/await_region.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/yield_region.h"

namespace abcd::source {
struct FoldRange {
  abc::CodeOffset begin, end;  // Half-open INPUT working byte offsets.
};
struct ProtocolRebaseInput {
  const abc::MethodBody& before;
  const abc::MethodBody& after;
  // Original-coordinate provenance, parallel to the respective instructions.
  // Empty before_origins means the input working offsets are original offsets.
  std::span<const abc::CodeOffset> before_origins, after_origins;
  std::span<const FoldRange> folds;
  std::span<const analysis::AwaitSite> awaits;
  std::span<const analysis::YieldSite> yields;
  abc::ProfileId profile;
};
enum class ProtocolRebaseError : std::uint8_t {
  kInput, kAnchor, kCollision, kDispatch, kResourceLimit, kWorkLimit
};
struct RebasedProtocols {
  std::vector<analysis::AwaitSite> awaits;
  std::vector<analysis::YieldSite> yields;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Compose an owner-certified protocol stream with a closed instruction fold.
// Only Site.pc/mode_pc are working coordinates. ALL origin fields (including
// resume/rejection and yield RETURN routes) remain original coordinates.
// A fold cannot absorb an anchor or bisect its retained suspend/mode protocol.
// Selected catch identity is checked for every surviving non-fold instruction.
// O((I+H+T+A+Y) log(I+H+T)), prepaid result and scratch; no raw opcode admission.
[[nodiscard]] std::variant<RebasedProtocols, ProtocolRebaseError> RebaseProtocols(
    const ProtocolRebaseInput&, analysis::SemanticBudget = {});
[[nodiscard]] std::variant<std::uint64_t, ProtocolRebaseError> VerifyRebasedProtocols(
    const ProtocolRebaseInput&, const RebasedProtocols&,
    analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
