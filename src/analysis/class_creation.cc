#include "abcd/analysis/class_creation.h"

namespace abcd::analysis {
std::variant<ClassCreation, CreationError> LiftClassCreation(
    const abc::DecodedInstruction& instruction) {
  std::uint64_t cache_max;
  switch (instruction.opcode) {
    case abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8:
      cache_max = UINT8_MAX;
      break;
    case abc::Opcode::kDEFINECLASSWITHBUFFER_IMM16_ID16_ID16_IMM16_V8:
      cache_max = UINT16_MAX;
      break;
    default:
      return CreationError::kUnsupportedOpcode;
  }
  // Pinned interpreter-inl.cpp 5178-5220: IC, constructor, buffer, length,
  // parent register. Lexical environment comes from the current frame.
  constexpr std::array kinds{
      abc::OperandKind::kImmediate, abc::OperandKind::kMethod,
      abc::OperandKind::kLiteralArray, abc::OperandKind::kImmediate,
      abc::OperandKind::kRegister};
  const std::array<std::uint64_t, 5> maxima{cache_max, UINT16_MAX, UINT16_MAX,
                                            UINT16_MAX, UINT8_MAX};
  if (instruction.operand_count != kinds.size())
    return CreationError::kMalformedInstruction;
  for (std::size_t i = 0; i < kinds.size(); ++i)
    if (instruction.operands[i].kind != kinds[i] ||
        instruction.operands[i].raw_bits > maxima[i])
      return CreationError::kMalformedInstruction;
  constexpr std::size_t kConstructor = 1, kLiteral = 2, kLength = 3,
                        kParent = 4;
  return ClassCreation{
      {static_cast<std::uint32_t>(instruction.operands[kConstructor].raw_bits)},
      {static_cast<std::uint32_t>(instruction.operands[kLiteral].raw_bits)},
      static_cast<std::uint16_t>(instruction.operands[kLength].raw_bits),
      {static_cast<std::uint32_t>(instruction.operands[kParent].raw_bits)}};
}
}  // namespace abcd::analysis
