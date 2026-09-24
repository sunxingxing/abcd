#ifndef ABCD_ANALYSIS_EXCEPTION_VALUE_FLOW_H_
#define ABCD_ANALYSIS_EXCEPTION_VALUE_FLOW_H_
#include "abcd/analysis/exception_ssa.h"
#include "abcd/analysis/register_value_flow.h"

namespace abcd::analysis {
enum class PendingExceptionValues : std::uint8_t { kUnknown, kLanguage };
struct ExceptionLoadFact {
  // Owner-proven Language result on normal completion, not a guard-fusion
  // certificate. Must name a reachable lexical/module load's Commit node.
  ExceptionNodeId commit;
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
      std::span<const ExceptionLoadFact> loads = {});
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
