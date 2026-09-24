#include "abcd/analysis/array_creation.h"

namespace abcd::analysis {
std::variant<BufferedArrayCreation, ArrayCreationError> LiftArrayCreation(
    const abc::DecodedInstruction& instruction) {
  std::uint64_t cache_max;
  switch (instruction.opcode) {
    case abc::Opcode::kCREATEARRAYWITHBUFFER_IMM8_ID16:
      cache_max = UINT8_MAX;
      break;
    case abc::Opcode::kCREATEARRAYWITHBUFFER_IMM16_ID16:
      cache_max = UINT16_MAX;
      break;
    default:
      return ArrayCreationError::kUnsupportedOpcode;
  }
  constexpr std::size_t kOperands = 2, kCache = 0, kLiteral = 1;
  if (instruction.operand_count != kOperands ||
      instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > cache_max ||
      instruction.operands[kLiteral].kind != abc::OperandKind::kLiteralArray ||
      instruction.operands[kLiteral].raw_bits > UINT16_MAX)
    return ArrayCreationError::kMalformedInstruction;
  return BufferedArrayCreation{
      {static_cast<std::uint32_t>(instruction.operands[kLiteral].raw_bits)},
      static_cast<std::uint16_t>(instruction.operands[kCache].raw_bits)};
}
}  // namespace abcd::analysis
