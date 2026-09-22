#include "abcd/analysis/named_invocation.h"

#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
std::variant<ReceiverCall, CallError> LiftReceiverCall(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  std::uint8_t count = 0;
  switch (instruction.opcode) {
    case abc::Opcode::kCALLTHIS0_IMM8_V8:
      break;
    case abc::Opcode::kCALLTHIS1_IMM8_V8_V8:
      count = 1;
      break;
    case abc::Opcode::kCALLTHIS2_IMM8_V8_V8_V8:
      count = 2;
      break;
    case abc::Opcode::kCALLTHIS3_IMM8_V8_V8_V8_V8:
      count = 3;
      break;
    default:
      return CallError::kUnsupportedOpcode;
  }
  constexpr std::size_t kCache = 0;
  constexpr std::size_t kReceiver = 1;
  constexpr std::size_t kArguments = 2;
  if (instruction.operand_count != kArguments + count ||
      instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > UINT8_MAX)
    return CallError::kMalformedInstruction;
  ReceiverCall result{};
  result.arguments.count = count;
  for (std::size_t i = kReceiver; i < kArguments + count; ++i) {
    const auto& operand = instruction.operands[i];
    if (operand.kind != abc::OperandKind::kRegister ||
        operand.raw_bits > UINT8_MAX)
      return CallError::kMalformedInstruction;
    if (operand.raw_bits >= frame_size) return CallError::kRegisterRange;
    const abc::RegisterId reg{static_cast<std::uint32_t>(operand.raw_bits)};
    if (i == kReceiver)
      result.receiver = reg;
    else
      result.arguments.values[i - kArguments] = reg;
  }
  return result;
}

std::optional<NamedInvocation> MatchNamedInvocation(
    std::span<const abc::BodyInstruction> block, std::size_t property,
    std::uint64_t frame_size) {
  if (property == 0 || property >= block.size() || property + 1 >= block.size())
    return {};
  const auto lifted = LiftReceiverCall(block[property + 1].decoded, frame_size);
  const auto* call = std::get_if<ReceiverCall>(&lifted);
  if (!call) return {};
  const auto read = LiftNamedRead(block[property].decoded);
  const auto* named = std::get_if<NamedRead>(&read);
  if (!named) return {};
  const auto copied =
      LiftScalarInstruction(block[property - 1].decoded, frame_size);
  const auto* copy = std::get_if<ScalarAssignment>(&copied);
  if (!copy) return {};
  const auto* value = std::get_if<ScalarValue>(&copy->expression);
  if (!value) return {};
  if (std::holds_alternative<Accumulator>(copy->destination)) {
    const auto* reg = std::get_if<abc::RegisterId>(value);
    if (!reg || *reg != call->receiver) return {};
  } else {
    if (std::get<abc::RegisterId>(copy->destination) != call->receiver ||
        !std::holds_alternative<Accumulator>(*value))
      return {};
  }
  return NamedInvocation{*named, *call};
}
}  // namespace abcd::analysis
