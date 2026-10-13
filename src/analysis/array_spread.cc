#include "abcd/analysis/array_spread.h"

namespace abcd::analysis {
std::variant<ArraySpread, ArraySpreadError> LiftArraySpread(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  if (instruction.opcode != abc::Opcode::kSTARRAYSPREAD_V8_V8)
    return ArraySpreadError::kUnsupportedOpcode;
  constexpr std::size_t kOperands = 2;
  if (instruction.operand_count != kOperands) return ArraySpreadError::kMalformedInstruction;
  for (std::size_t i = 0; i < kOperands; ++i) {
    const auto& operand = instruction.operands[i];
    if (operand.kind != abc::OperandKind::kRegister || operand.raw_bits > UINT8_MAX)
      return ArraySpreadError::kMalformedInstruction;
    if (operand.raw_bits >= frame_size) return ArraySpreadError::kRegisterRange;
  }
  return ArraySpread{{static_cast<std::uint32_t>(instruction.operands[0].raw_bits)},
                     {static_cast<std::uint32_t>(instruction.operands[1].raw_bits)}};
}
}  // namespace abcd::analysis
