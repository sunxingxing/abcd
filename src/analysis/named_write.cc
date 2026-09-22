#include "abcd/analysis/named_write.h"

namespace abcd::analysis {
std::variant<NamedWrite, NamedWriteError> LiftNamedWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  constexpr std::size_t kCache = 0;
  constexpr std::size_t kName = 1;
  constexpr std::size_t kObject = 2;
  constexpr std::size_t kOperands = 3;
  std::uint64_t cache_max;
  switch (instruction.opcode) {
    case abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8:
      cache_max = UINT8_MAX;
      break;
    case abc::Opcode::kSTOBJBYNAME_IMM16_ID16_V8:
      cache_max = UINT16_MAX;
      break;
    default:
      return NamedWriteError::kUnsupportedOpcode;
  }
  if (instruction.operand_count != kOperands ||
      instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > cache_max ||
      instruction.operands[kName].kind != abc::OperandKind::kString ||
      instruction.operands[kName].raw_bits > UINT16_MAX ||
      instruction.operands[kObject].kind != abc::OperandKind::kRegister ||
      instruction.operands[kObject].raw_bits > UINT8_MAX)
    return NamedWriteError::kMalformedInstruction;
  if (instruction.operands[kObject].raw_bits >= frame_size)
    return NamedWriteError::kRegisterRange;
  return NamedWrite{
      {static_cast<std::uint32_t>(instruction.operands[kName].raw_bits)},
      {static_cast<std::uint32_t>(instruction.operands[kObject].raw_bits)}};
}
}  // namespace abcd::analysis
