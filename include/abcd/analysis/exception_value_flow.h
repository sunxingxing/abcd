#ifndef ABCD_ANALYSIS_EXCEPTION_VALUE_FLOW_H_
#define ABCD_ANALYSIS_EXCEPTION_VALUE_FLOW_H_
#include "abcd/analysis/exception_ssa.h"
#include "abcd/analysis/register_value_flow.h"

namespace abcd::analysis {
enum class PendingExceptionValues : std::uint8_t { kUnknown, kLanguage };
enum class ExceptionLoadProof : std::uint8_t {
  kLanguage, kCallerGuardedModule, kCatchGuardedModule
};
struct ExceptionLoadFact {
  // Must name a reachable lexical/module load's Commit node. kLanguage is an
  // owner-proven normal result, not permission for guard fusion.
  ExceptionNodeId commit;
  // Guarded module facts describe the LOWERED pair, not the raw load
  // (which may return Hole). The owner must emit its certified fused read.
  ExceptionLoadProof proof = ExceptionLoadProof::kLanguage;
};
struct ExceptionPendingFact {
  // Owner-proven pending payload on this reachable Before site's exception
  // edge, not its normal result. Explicit THROW uses its SSA operand instead.
  ExceptionNodeId before;
  ValueKinds kinds;
};
class ExceptionValueFlow {
 public:
  ExceptionValueFlow(ExceptionValueFlow&&) noexcept = default;
  ExceptionValueFlow& operator=(ExceptionValueFlow&&) noexcept = default;
  // Requires matching immutable flow/SSA. Entry facts are owner premises.
  // Pending exceptions stay unknown absent an explicit owner premise covering
  // runtime/user-code exceptions. Explicit throws retain their SSA categories
  // even under that premise. This is not source admission: operation inputs,
  // captured/module loads and environments still need independent validation.
  [[nodiscard]] static std::variant<ExceptionValueFlow, ValueFlowFailure> Build(
      const ExceptionFlow& flow, const ExceptionSsa& ssa,
      std::span<const EntryValueFact> entries, IdfQuota& quota,
      IdfBudget budget = {},
      PendingExceptionValues pending = PendingExceptionValues::kUnknown,
      std::span<const ExceptionLoadFact> loads = {},
      // Per-edge facts override the global pending premise at their sites.
      // Unknown remains the default elsewhere. Owners must cover all possible
      // runtime and user-code sources on each certified edge; opcode names or
      // a NewJSError call alone do not establish that contract.
      std::span<const ExceptionPendingFact> pending_facts = {});
  [[nodiscard]] std::span<const ValueKinds> Facts() const {
    return {facts_.get(), facts_ ? count_ : 0};
  }
  [[nodiscard]] std::size_t AllocatedBytes() const { return Facts().size_bytes(); }
  ValueFlowWork work;
 private:
  ExceptionValueFlow() = default;
  std::unique_ptr<ValueKinds[]> facts_;
  std::size_t count_ = 0;
};
}  // namespace abcd::analysis
#endif
