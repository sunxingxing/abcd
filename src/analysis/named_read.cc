#include "abcd/analysis/named_read.h"

namespace abcd::analysis {
std::variant<NamedRead, NamedReadError> LiftNamedRead(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  using abc::Opcode;
  if (instruction.opcode == Opcode::kDEPRECATED_LDOBJBYNAME_PREF_ID32_V8) {
    if (instruction.operand_count != 2 ||
        instruction.operands[0].kind != abc::OperandKind::kString ||
        instruction.operands[0].raw_bits > UINT16_MAX ||
        instruction.operands[1].kind != abc::OperandKind::kRegister ||
        instruction.operands[1].raw_bits >= frame_size)
      return NamedReadError::kMalformedInstruction;
    return NamedRead{{static_cast<std::uint32_t>(instruction.operands[0].raw_bits)}, PropertyLookup::kObject,
        abc::RegisterId{static_cast<std::uint32_t>(instruction.operands[1].raw_bits)}};
  }
  constexpr std::size_t kCacheOperand = 0;
  constexpr std::size_t kNameOperand = 1;
  constexpr std::size_t kOperandCount = 2;
  std::uint64_t cache_max;
  auto lookup = PropertyLookup::kObject;
  switch (instruction.opcode) {
    case Opcode::kLDSUPERBYNAME_IMM8_ID16:
      lookup = PropertyLookup::kSuper; cache_max = UINT8_MAX; break;
    case Opcode::kLDSUPERBYNAME_IMM16_ID16:
      lookup = PropertyLookup::kSuper; cache_max = UINT16_MAX; break;
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
      instruction.operands[kNameOperand].raw_bits)}, lookup};
}
}  // namespace abcd::analysis
