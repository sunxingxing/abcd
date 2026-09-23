#include "abcd/analysis/indexed_write.h"

#include <array>

namespace abcd::analysis {
std::variant<IndexedWrite, IndexedWriteError> LiftIndexedWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  using abc::Opcode;
  using abc::OperandKind;
  constexpr std::uint64_t kByteMax = UINT8_MAX;
  constexpr std::uint64_t kWordMax = UINT16_MAX;
  constexpr std::uint64_t kDwordMax = UINT32_MAX;
  std::array kinds{OperandKind::kImmediate, OperandKind::kRegister,
                   OperandKind::kImmediate};
  std::array maxima{kByteMax, kByteMax, kWordMax};
  std::size_t count = kinds.size();
  std::size_t object_operand = 1;
  std::size_t index_operand = 2;
  switch (instruction.opcode) {
    case Opcode::kSTOWNBYINDEX_IMM8_V8_IMM16:
      break;
    case Opcode::kSTOWNBYINDEX_IMM16_V8_IMM16:
      maxima[0] = kWordMax;
      break;
    case Opcode::kWIDE_STOWNBYINDEX_PREF_V8_IMM32:
      count = 2;
      kinds[0] = OperandKind::kRegister;
      kinds[1] = OperandKind::kImmediate;
      maxima[1] = kDwordMax;
      object_operand = 0;
      index_operand = 1;
      break;
    default:
      return IndexedWriteError::kUnsupportedOpcode;
  }
  if (instruction.operand_count != count)
    return IndexedWriteError::kMalformedInstruction;
  for (std::size_t i = 0; i < count; ++i)
    if (instruction.operands[i].kind != kinds[i] ||
        instruction.operands[i].raw_bits > maxima[i])
      return IndexedWriteError::kMalformedInstruction;
  const auto object = instruction.operands[object_operand].raw_bits;
  if (object >= frame_size) return IndexedWriteError::kRegisterRange;
  return IndexedWrite{{static_cast<std::uint32_t>(object)},
                      static_cast<std::uint32_t>(
                          instruction.operands[index_operand].raw_bits)};
}
}  // namespace abcd::analysis
