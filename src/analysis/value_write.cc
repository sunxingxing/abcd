#include "abcd/analysis/value_write.h"
#include <bit>

namespace abcd::analysis {
std::variant<ValueWrite, ValueWriteError> LiftValueWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  // Pinned interpreter-inl.cpp:5811-5896 and runtime_stubs-inl.h:782:
  // RuntimeStObjByIndex uses SetProperty(..., true), not DefineOwnProperty.
  // Keep own-index stores on their distinct IndexedWrite/literal proof path.
  std::uint64_t cache_max;
  bool indexed = false, wide = false;
  auto lookup = PropertyLookup::kObject;
  switch (instruction.opcode) {
    case abc::Opcode::kSTOBJBYINDEX_IMM8_V8_IMM16:
      indexed = true; cache_max = UINT8_MAX; break;
    case abc::Opcode::kSTOBJBYINDEX_IMM16_V8_IMM16:
      indexed = true; cache_max = UINT16_MAX; break;
    case abc::Opcode::kWIDE_STOBJBYINDEX_PREF_V8_IMM32:
      indexed = true; wide = true; cache_max = 0; break;
    case abc::Opcode::kSTSUPERBYVALUE_IMM8_V8_V8:
      lookup = PropertyLookup::kSuper; cache_max = UINT8_MAX; break;
    case abc::Opcode::kSTSUPERBYVALUE_IMM16_V8_V8:
      lookup = PropertyLookup::kSuper; cache_max = UINT16_MAX; break;
    case abc::Opcode::kSTOBJBYVALUE_IMM8_V8_V8:
      cache_max = UINT8_MAX;
      break;
    case abc::Opcode::kSTOBJBYVALUE_IMM16_V8_V8:
      cache_max = UINT16_MAX;
      break;
    default:
      return ValueWriteError::kUnsupportedOpcode;
  }
  if (indexed) {
    const std::size_t object = wide ? 0 : 1, key = object + 1;
    if (instruction.operand_count != (wide ? 2 : 3) ||
        (!wide && (instruction.operands[0].kind != abc::OperandKind::kImmediate ||
                   instruction.operands[0].raw_bits > cache_max)) ||
        instruction.operands[object].kind != abc::OperandKind::kRegister ||
        instruction.operands[object].raw_bits > UINT8_MAX ||
        instruction.operands[key].kind != abc::OperandKind::kImmediate ||
        instruction.operands[key].raw_bits > (wide ? UINT32_MAX : UINT16_MAX))
      return ValueWriteError::kMalformedInstruction;
    if (instruction.operands[object].raw_bits >= frame_size)
      return ValueWriteError::kRegisterRange;
    return ValueWrite{abc::RegisterId{static_cast<std::uint32_t>(instruction.operands[object].raw_bits)},
        Float64Bits{std::bit_cast<std::uint64_t>(static_cast<double>(instruction.operands[key].raw_bits))}};
  }
  constexpr std::size_t kCache = 0;
  constexpr std::size_t kObject = 1;
  constexpr std::size_t kKey = 2;
  constexpr std::size_t kOperandCount = 3;
  if (instruction.operand_count != kOperandCount ||
      instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > cache_max ||
      instruction.operands[kObject].kind != abc::OperandKind::kRegister ||
      instruction.operands[kObject].raw_bits > UINT8_MAX ||
      instruction.operands[kKey].kind != abc::OperandKind::kRegister ||
      instruction.operands[kKey].raw_bits > UINT8_MAX)
    return ValueWriteError::kMalformedInstruction;
  if (instruction.operands[kObject].raw_bits >= frame_size ||
      instruction.operands[kKey].raw_bits >= frame_size)
    return ValueWriteError::kRegisterRange;
  return ValueWrite{
      {static_cast<std::uint32_t>(instruction.operands[kObject].raw_bits)},
      abc::RegisterId{static_cast<std::uint32_t>(instruction.operands[kKey].raw_bits)}, lookup};
}
}  // namespace abcd::analysis
