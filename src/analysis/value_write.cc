#include "abcd/analysis/value_write.h"

namespace abcd::analysis {
std::variant<ValueWrite, ValueWriteError> LiftValueWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  std::uint64_t cache_max;
  switch (instruction.opcode) {
    case abc::Opcode::kSTOBJBYVALUE_IMM8_V8_V8:
      cache_max = UINT8_MAX;
      break;
    case abc::Opcode::kSTOBJBYVALUE_IMM16_V8_V8:
      cache_max = UINT16_MAX;
      break;
    default:
      return ValueWriteError::kUnsupportedOpcode;
  }
  constexpr std::size_t kCache = 0;
  constexpr std::size_t kObject = 1;
  constexpr std::size_t kKey = 2;
  constexpr std::size_t kOperandCount = 3;
  if (instruction.operand_count != kOperandCount ||
      instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > cache_max ||
      instruction.operands[kObject].kind != abc::OperandKind::kRegister ||
      instruction.operands[kObject].raw_bits > UINT8_MAX ||
      instruction.operands[kKey].kind != abc::OperandKind::kRegister ||
      instruction.operands[kKey].raw_bits > UINT8_MAX)
    return ValueWriteError::kMalformedInstruction;
  if (instruction.operands[kObject].raw_bits >= frame_size ||
      instruction.operands[kKey].raw_bits >= frame_size)
    return ValueWriteError::kRegisterRange;
  return ValueWrite{
      {static_cast<std::uint32_t>(instruction.operands[kObject].raw_bits)},
      {static_cast<std::uint32_t>(instruction.operands[kKey].raw_bits)}};
}
}  // namespace abcd::analysis
