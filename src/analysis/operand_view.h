#ifndef ABCD_ANALYSIS_OPERAND_VIEW_H_
#define ABCD_ANALYSIS_OPERAND_VIEW_H_

#include "call_operands.h"

namespace abcd::analysis {
enum class OperandViewError : std::uint8_t {
  kUnsupported, kMismatch, kResourceLimit, kWorkLimit
};
// One indexed interface for scalar, call and argument-snapshot inputs. This view
// owns compact descriptions only; expanded use storage belongs to ValueProgram.
class OperandView {
 public:
  [[nodiscard]] static std::variant<OperandView, OperandViewError> Build(
      const SemanticInstruction& instruction, SemanticBudget budget = {});
  [[nodiscard]] std::uint32_t Count() const;
  [[nodiscard]] std::optional<InputOperand> At(std::uint32_t index) const;
  [[nodiscard]] std::optional<ScalarSlot> Write() const;
  [[nodiscard]] std::uint64_t Work() const;
 private:
  explicit OperandView(ValueOperands scalar) : operands_(std::move(scalar)) {}
  explicit OperandView(CallOperands call) : operands_(std::move(call)) {}
  explicit OperandView(ArgumentWindow window) : operands_(window) {}
  std::variant<ValueOperands, CallOperands, ArgumentWindow> operands_;
};
}  // namespace abcd::analysis
#endif
