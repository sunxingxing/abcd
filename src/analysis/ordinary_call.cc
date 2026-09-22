#include "abcd/analysis/ordinary_call.h"

namespace abcd::analysis {
std::uint32_t OrdinaryCall::Count() const {
  return std::visit([](const auto& args) { return args.count; }, arguments);
}

std::optional<abc::RegisterId> OrdinaryCall::At(std::uint32_t position) const {
  if (position >= Count()) return {};
  if (const auto* fixed = std::get_if<CallRegisters>(&arguments)) {
    if (position >= fixed->values.size()) return {};
    return fixed->values[position];
  }
  const auto range = std::get<CallRange>(arguments);
  if (position > UINT32_MAX - range.first.value) return {};
  return abc::RegisterId{range.first.value + position};
}

std::variant<OrdinaryCall, CallError> LiftOrdinaryCall(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  constexpr std::size_t kCache = 0;
  constexpr std::size_t kRangeCount = 1;
  constexpr std::size_t kRangeFirst = 2;
  constexpr std::size_t kRangeOperands = 3;
  std::uint8_t count = 0;
  bool range = false;
  switch (instruction.opcode) {
    case abc::Opcode::kCALLARG0_IMM8:
      break;
    case abc::Opcode::kCALLARG1_IMM8_V8:
      count = 1;
      break;
    case abc::Opcode::kCALLARGS2_IMM8_V8_V8:
      count = 2;
      break;
    case abc::Opcode::kCALLARGS3_IMM8_V8_V8_V8:
      count = 3;
      break;
    case abc::Opcode::kCALLRANGE_IMM8_IMM8_V8:
      range = true;
      break;
    default:
      return CallError::kUnsupportedOpcode;
  }
  const auto operands = range ? kRangeOperands : std::size_t(count) + 1;
  if (instruction.operand_count != operands ||
      instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > UINT8_MAX)
    return CallError::kMalformedInstruction;
  if (range) {
    const auto& size = instruction.operands[kRangeCount];
    const auto& first = instruction.operands[kRangeFirst];
    if (size.kind != abc::OperandKind::kImmediate ||
        size.raw_bits > UINT8_MAX ||
        first.kind != abc::OperandKind::kRegister || first.raw_bits > UINT8_MAX)
      return CallError::kMalformedInstruction;
    if (size.raw_bits != 0 && (first.raw_bits >= frame_size ||
                               size.raw_bits > frame_size - first.raw_bits))
      return CallError::kRegisterRange;
    return OrdinaryCall{CallRange{{static_cast<std::uint32_t>(first.raw_bits)},
                                  static_cast<std::uint8_t>(size.raw_bits)}};
  }
  CallRegisters registers;
  registers.count = count;
  for (std::size_t i = 0; i < count; ++i) {
    const auto& operand = instruction.operands[i + 1];
    if (operand.kind != abc::OperandKind::kRegister ||
        operand.raw_bits > UINT8_MAX)
      return CallError::kMalformedInstruction;
    if (operand.raw_bits >= frame_size) return CallError::kRegisterRange;
    registers.values[i] = {static_cast<std::uint32_t>(operand.raw_bits)};
  }
  return OrdinaryCall{registers};
}
}  // namespace abcd::analysis
