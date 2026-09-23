#include "abcd/analysis/module_store.h"

namespace abcd::analysis {
std::variant<ModuleStore, ModuleStoreError> LiftModuleStore(
    const abc::DecodedInstruction& instruction) {
  std::uint64_t limit = UINT8_MAX;
  switch (instruction.opcode) {
    case abc::Opcode::kWIDE_STMODULEVAR_PREF_IMM16:
      limit = UINT16_MAX;
      [[fallthrough]];
    case abc::Opcode::kSTMODULEVAR_IMM8:
      break;
    default:
      return ModuleStoreError::kUnsupportedOpcode;
  }
  constexpr std::size_t kSlot = 0, kOperands = 1;
  if (instruction.operand_count != kOperands ||
      instruction.operands[kSlot].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kSlot].raw_bits > limit)
    return ModuleStoreError::kMalformedInstruction;
  return ModuleStore{{static_cast<std::uint16_t>(
      instruction.operands[kSlot].raw_bits)}};
}
}  // namespace abcd::analysis
