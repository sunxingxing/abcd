#include "abcd/analysis/object_creation.h"

namespace abcd::analysis {
std::variant<EmptyObjectCreation, BufferedObjectCreation, ObjectCreationError>
LiftObjectCreation(
    const abc::DecodedInstruction& instruction) {
  using abc::Opcode;
  if (instruction.opcode == Opcode::kCREATEEMPTYOBJECT) {
    if (instruction.operand_count != 0)
      return ObjectCreationError::kMalformedInstruction;
    return EmptyObjectCreation{};
  }
  std::uint64_t cache_limit = 0;
  switch (instruction.opcode) {
    case Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16:
      cache_limit = UINT8_MAX;
      break;
    case Opcode::kCREATEOBJECTWITHBUFFER_IMM16_ID16:
      cache_limit = UINT16_MAX;
      break;
    default:
      return ObjectCreationError::kUnsupportedOpcode;
  }
  if (instruction.operand_count != 2 ||
      instruction.operands[0].kind != abc::OperandKind::kImmediate ||
      instruction.operands[1].kind != abc::OperandKind::kLiteralArray ||
      instruction.operands[0].raw_bits > cache_limit ||
      instruction.operands[1].raw_bits > UINT16_MAX)
    return ObjectCreationError::kMalformedInstruction;
  return BufferedObjectCreation{{static_cast<std::uint32_t>(
      instruction.operands[1].raw_bits)}};
}
}  // namespace abcd::analysis
