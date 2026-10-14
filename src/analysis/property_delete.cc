#include "abcd/analysis/property_delete.h"

namespace abcd::analysis {
std::variant<PropertyDelete, ScalarError> LiftPropertyDelete(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  if (instruction.opcode != abc::Opcode::kDELOBJPROP_V8)
    return ScalarError::kUnsupportedOpcode;
  if (instruction.operand_count != 1 ||
      instruction.operands[0].kind != abc::OperandKind::kRegister ||
      instruction.operands[0].raw_bits > UINT8_MAX)
    return ScalarError::kMalformedInstruction;
  const auto reg = instruction.operands[0].raw_bits;
  if (reg >= frame_size) return ScalarError::kRegisterRange;
  return PropertyDelete{{static_cast<std::uint32_t>(reg)}};
}
}  // namespace abcd::analysis
