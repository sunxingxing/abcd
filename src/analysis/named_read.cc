#include "abcd/analysis/named_read.h"

namespace abcd::analysis {
std::variant<NamedRead, NamedReadError> LiftNamedRead(
    const abc::DecodedInstruction& instruction) {
  using abc::Opcode;
  constexpr std::size_t kCacheOperand = 0;
  constexpr std::size_t kNameOperand = 1;
  constexpr std::size_t kOperandCount = 2;
  std::uint64_t cache_max;
  switch (instruction.opcode) {
    case Opcode::kLDOBJBYNAME_IMM8_ID16:
      cache_max = UINT8_MAX;
      break;
    case Opcode::kLDOBJBYNAME_IMM16_ID16:
      cache_max = UINT16_MAX;
      break;
    default:
      return NamedReadError::kUnsupportedOpcode;
  }
  if (instruction.operand_count != kOperandCount ||
      instruction.operands[kCacheOperand].kind !=
          abc::OperandKind::kImmediate ||
      instruction.operands[kCacheOperand].raw_bits > cache_max ||
      instruction.operands[kNameOperand].kind != abc::OperandKind::kString ||
      instruction.operands[kNameOperand].raw_bits > UINT16_MAX)
    return NamedReadError::kMalformedInstruction;
  return NamedRead{{static_cast<std::uint32_t>(
      instruction.operands[kNameOperand].raw_bits)}};
}
}  // namespace abcd::analysis
