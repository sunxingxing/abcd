#include "operand_view.h"

namespace abcd::analysis {
std::variant<OperandView, OperandViewError> OperandView::Build(
    const SemanticInstruction& instruction, SemanticBudget budget) {
  using Error = OperandViewError;
  if (budget.bytes < sizeof(OperandView)) return Error::kResourceLimit;
  if (std::holds_alternative<OrdinaryCall>(instruction.operation) ||
      std::holds_alternative<ReceiverCall>(instruction.operation) ||
      std::holds_alternative<ConstructorCall>(instruction.operation)) {
    auto result = CallOperands::Build(instruction, budget);
    if (auto* call = std::get_if<CallOperands>(&result)) return OperandView{std::move(*call)};
    switch (std::get<CallOperandError>(result)) {
      case CallOperandError::kUnsupported: return Error::kUnsupported;
      case CallOperandError::kCount:
      case CallOperandError::kSlot:
      case CallOperandError::kWrite: return Error::kMismatch;
      case CallOperandError::kResourceLimit: return Error::kResourceLimit;
      case CallOperandError::kWorkLimit: return Error::kWorkLimit;
    }
  }
  auto result = DescribeValueOperands(instruction);
  if (auto* scalar = std::get_if<ValueOperands>(&result)) return OperandView{std::move(*scalar)};
  return std::get<OperandError>(result) == OperandError::kUnsupportedOperation
      ? Error::kUnsupported : Error::kMismatch;
}
std::uint32_t OperandView::Count() const {
  if (const auto* scalar = std::get_if<ValueOperands>(&operands_)) return scalar->count;
  return std::get<CallOperands>(operands_).Count();
}
std::optional<InputOperand> OperandView::At(std::uint32_t index) const {
  if (const auto* scalar = std::get_if<ValueOperands>(&operands_)) {
    if (index >= scalar->count) return {};
    return scalar->operands[index];
  }
  return std::get<CallOperands>(operands_).At(index);
}
std::optional<ScalarSlot> OperandView::Write() const {
  if (const auto* scalar = std::get_if<ValueOperands>(&operands_)) return scalar->write;
  return Accumulator{};
}
std::uint64_t OperandView::Work() const {
  if (const auto* call = std::get_if<CallOperands>(&operands_)) return call->Work();
  return 0;
}
}  // namespace abcd::analysis
