#include "call_operands.h"

#include <limits>
#include <type_traits>

namespace abcd::analysis {
namespace {
// VM call contracts read the callee once, then an explicit receiver only for
// receiver calls; newTarget is implicit and creates no extra read occurrence.
constexpr std::uint32_t kCalleeOperands = 1;
constexpr std::uint32_t kReceiverPrefix = kCalleeOperands + 1;
bool SameSlot(const ScalarSlot& left, const ScalarValue& right) {
  if (std::holds_alternative<Accumulator>(left)) return std::holds_alternative<Accumulator>(right);
  const auto* reg = std::get_if<abc::RegisterId>(&right);
  return reg && *reg == std::get<abc::RegisterId>(left);
}
bool ValidArguments(const std::variant<CallRegisters, CallRange>& arguments) {
  if (const auto* fixed = std::get_if<CallRegisters>(&arguments))
    return fixed->count <= fixed->values.size();
  const auto range = std::get<CallRange>(arguments);
  return range.count == 0 || range.count - 1U <=
      std::numeric_limits<decltype(range.first.value)>::max() - range.first.value;
}
}  // namespace

std::uint32_t CallOperands::Count() const {
  return std::visit([](const auto& call) -> std::uint32_t {
    using T = std::decay_t<decltype(call)>;
    if constexpr (std::is_same_v<T, SuperRange>) return call.count + (call.callee == SuperCalleeSource::kAccumulator ? 1U : 0U);
    else if constexpr (std::is_same_v<T, ConstructorCall>) return call.inputs.count;
    else if constexpr (std::is_same_v<T, ReceiverCall>) return call.Count() + kReceiverPrefix;
    else return call.Count() + kCalleeOperands;
  }, call_);
}
std::optional<InputOperand> CallOperands::At(std::uint32_t index) const {
  if (index >= Count()) return {};
  return std::visit([&](const auto& call) -> std::optional<InputOperand> {
    using T = std::decay_t<decltype(call)>;
    const auto operand = [&](OperandRole role, ScalarValue value) {
      return InputOperand{role, value, ReadPurpose::kOperand, index};
    };
    if constexpr (std::is_same_v<T, SuperRange>) {
      const auto prefix = call.callee == SuperCalleeSource::kAccumulator ? 1U : 0U;
      if (prefix && index == 0) return operand(OperandRole::kConstructor, Accumulator{});
      return operand(OperandRole::kArgument, abc::RegisterId{call.first.value + index - prefix});
    } else if constexpr (std::is_same_v<T, ConstructorCall>) {
      if (index == 0) return operand(OperandRole::kConstructor, call.inputs.first);
      const auto argument = call.ArgumentAt(index - kCalleeOperands);
      if (argument) return operand(OperandRole::kArgument, *argument);
    } else {
      if (index == 0)
        return operand(OperandRole::kCallee, std::visit([](auto slot) -> ScalarValue { return slot; }, call.callee));
      auto argument_index = index - kCalleeOperands;
      if constexpr (std::is_same_v<T, ReceiverCall>) {
        if (index == kCalleeOperands) return operand(OperandRole::kReceiver, call.receiver);
        argument_index = index - kReceiverPrefix;
      }
      const auto argument = call.At(argument_index);
      if (argument) return operand(OperandRole::kArgument, *argument);
    }
    return {};
  }, call_);
}

std::variant<CallOperands, CallOperandError> CallOperands::Build(
    const SemanticInstruction& instruction, SemanticBudget budget) {
  using Error = CallOperandError;
  if (budget.bytes < sizeof(CallOperands)) return Error::kResourceLimit;
  std::optional<CallOperands> plan;
  if (const auto* call = std::get_if<OrdinaryCall>(&instruction.operation)) {
    if (!ValidArguments(call->arguments)) return Error::kCount;
    plan = CallOperands{*call};
  } else if (const auto* call = std::get_if<ReceiverCall>(&instruction.operation)) {
    if (!ValidArguments(call->arguments)) return Error::kCount;
    plan = CallOperands{*call};
  } else if (const auto* call = std::get_if<ConstructorCall>(&instruction.operation)) {
    if (call->inputs.count == 0 || !ValidArguments(call->inputs)) return Error::kCount;
    plan = CallOperands{*call};
  } else if (const auto* call = std::get_if<SuperRange>(&instruction.operation)) {
    if (!ValidArguments(CallRange{call->first, call->count})) return Error::kCount;
    plan = CallOperands{*call};
  } else return Error::kUnsupported;
  const auto count = plan->Count();
  // Encoded range counts remain uint16, but callee/receiver add occurrences.
  // The shared SSA/value occurrence index must not inherit the encoding width.
  const auto& effect = instruction.effects.normal_registers;
  if (effect.read_count > effect.reads.size() ||
      count != std::uint64_t(effect.read_count) +
          (effect.range ? effect.range->count : 0U)) return Error::kCount;
  if (effect.range && effect.range->count != 0 &&
      effect.range->count - 1U > UINT32_MAX - effect.range->first.value)
    return Error::kCount;
  if (!effect.write || !std::holds_alternative<Accumulator>(*effect.write)) return Error::kWrite;
  if (count > budget.work) return Error::kWorkLimit;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto expected = plan->At(i);
    if (!expected) return Error::kSlot;
    ScalarSlot actual;
    if (i < effect.read_count) actual = effect.reads[i];
    else {
      const auto offset = i - effect.read_count;
      const auto first = effect.range->first.value;
      actual = abc::RegisterId{static_cast<decltype(first)>(first + offset)};
    }
    if (!SameSlot(actual, expected->value)) return Error::kSlot;
  }
  plan->work_ = count;
  return *plan;
}
}  // namespace abcd::analysis
