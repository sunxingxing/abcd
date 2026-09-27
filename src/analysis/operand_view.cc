#include "operand_view.h"

namespace abcd::analysis {
namespace {
constexpr std::uint64_t kSnapshotWork = 1;
}  // namespace
std::variant<OperandView, OperandViewError> OperandView::Build(
    const SemanticInstruction& instruction, SemanticBudget budget) {
  using Error = OperandViewError;
  if (budget.bytes < sizeof(OperandView)) return Error::kResourceLimit;
  if (const auto* snapshot = std::get_if<ArgumentSnapshot>(&instruction.operation)) {
    if (budget.work < kSnapshotWork) return Error::kWorkLimit;
    const auto& registers = instruction.effects.normal_registers;
    constexpr auto kSnapshotEffects =
        LanguageEffect::kArgumentsRead | LanguageEffect::kAllocation;
    if (registers.read_count != 0 || !registers.range ||
        registers.range->first != snapshot->window.First() ||
        registers.range->count != snapshot->window.Count() ||
        !registers.write || !std::holds_alternative<Accumulator>(*registers.write) ||
        instruction.effects.normal != kSnapshotEffects ||
        instruction.effects.before_throw != kSnapshotEffects ||
        instruction.effects.exceptional != ExceptionalCompletion::kMayThrow)
      return Error::kMismatch;
    return OperandView{snapshot->window};
  }
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
  if (const auto* window = std::get_if<ArgumentWindow>(&operands_)) return window->Count();
  return std::get<CallOperands>(operands_).Count();
}
std::optional<InputOperand> OperandView::At(std::uint32_t index) const {
  if (const auto* window = std::get_if<ArgumentWindow>(&operands_)) {
    const auto slot = window->At(index);
    if (!slot) return {};
    // The actual invocation count predicates each read. Preserve its SSA
    // dependency without licensing eager JS evaluation of absent arguments.
    return InputOperand{OperandRole::kArgument, *slot, ReadPurpose::kProtocol, index};
  }
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
  if (std::holds_alternative<ArgumentWindow>(operands_)) return kSnapshotWork;
  if (const auto* call = std::get_if<CallOperands>(&operands_)) return call->Work();
  return 0;
}
}  // namespace abcd::analysis
