#ifndef ABCD_ANALYSIS_CALL_OPERANDS_H_
#define ABCD_ANALYSIS_CALL_OPERANDS_H_

#include "value_operands.h"

namespace abcd::analysis {
enum class CallOperandError : std::uint8_t {
  kUnsupported, kCount, kSlot, kWrite, kResourceLimit, kWorkLimit
};
// Compact typed occurrence view. No expanded argument array and no borrowed
// instruction pointer. Equal registers in different roles remain separate
// occurrences. This is NOT an evaluation-order or source-call certificate.
class CallOperands {
 public:
  [[nodiscard]] static std::variant<CallOperands, CallOperandError> Build(
      const SemanticInstruction& instruction, SemanticBudget budget = {});
  [[nodiscard]] std::uint32_t Count() const;
  [[nodiscard]] std::optional<InputOperand> At(std::uint32_t index) const;
  [[nodiscard]] std::uint64_t Work() const { return work_; }
 private:
  using Call = std::variant<OrdinaryCall, ReceiverCall, ConstructorCall, SuperRange>;
  explicit CallOperands(Call call) : call_(std::move(call)) {}
  Call call_;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::analysis
#endif
