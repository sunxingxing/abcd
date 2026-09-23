#include "abcd/analysis/own_write.h"

namespace abcd::analysis {
std::variant<OwnWrite, OwnWriteError> LiftOwnWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  OwnWriteKind kind;
  std::uint64_t cache_max;
  switch (instruction.opcode) {
    case abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8:
      kind = OwnWriteKind::kStore;
      cache_max = UINT8_MAX;
      break;
    case abc::Opcode::kSTOWNBYNAME_IMM16_ID16_V8:
      kind = OwnWriteKind::kStore;
      cache_max = UINT16_MAX;
      break;
    case abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8:
    case abc::Opcode::kDEFINEPROPERTYBYNAME_IMM8_ID16_V8:
      kind = OwnWriteKind::kDefineField;
      cache_max = UINT8_MAX;
      break;
    default:
      return OwnWriteError::kUnsupportedOpcode;
  }
  // Pinned interpreter-inl.cpp STOWNBYNAME / DEFINEFIELDBYNAME handlers:
  // IC slot, string pool index, receiver register; value is accumulator.
  constexpr std::size_t kCache = 0, kName = 1, kObject = 2, kOperandCount = 3;
  if (instruction.operand_count != kOperandCount ||
      instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > cache_max ||
      instruction.operands[kName].kind != abc::OperandKind::kString ||
      instruction.operands[kName].raw_bits > UINT16_MAX ||
      instruction.operands[kObject].kind != abc::OperandKind::kRegister ||
      instruction.operands[kObject].raw_bits > UINT8_MAX)
    return OwnWriteError::kMalformedInstruction;
  if (instruction.operands[kObject].raw_bits >= frame_size)
    return OwnWriteError::kRegisterRange;
  return OwnWrite{
      kind,
      {static_cast<std::uint32_t>(instruction.operands[kName].raw_bits)},
      {static_cast<std::uint32_t>(instruction.operands[kObject].raw_bits)}};
}
}  // namespace abcd::analysis
