#include "abcd/analysis/indexed_read.h"

#include <array>

namespace abcd::analysis {
std::variant<IndexedRead, IndexedReadError> LiftIndexedRead(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  using abc::Opcode;
  using abc::OperandKind;
  constexpr std::size_t kFirstOperand = 0;
  constexpr std::size_t kSecondOperand = 1;
  constexpr std::uint64_t kByteMax = UINT8_MAX;
  constexpr std::uint64_t kShortMax = UINT16_MAX;
  constexpr std::uint64_t kIndexMax = UINT32_MAX;
  std::array kinds{OperandKind::kImmediate, OperandKind::kImmediate};
  std::array maxima{kByteMax, kShortMax};
  std::size_t operand_count = kinds.size();
  std::size_t index_operand = kSecondOperand;
  switch (instruction.opcode) {
    case Opcode::kLDOBJBYINDEX_IMM8_IMM16:
      break;
    case Opcode::kLDOBJBYINDEX_IMM16_IMM16:
      maxima[kFirstOperand] = kShortMax;
      break;
    case Opcode::kWIDE_LDOBJBYINDEX_PREF_IMM32:
      operand_count = 1;
      index_operand = kFirstOperand;
      maxima[kFirstOperand] = kIndexMax;
      break;
    case Opcode::kDEPRECATED_LDOBJBYINDEX_PREF_V8_IMM32:
      kinds[kFirstOperand] = OperandKind::kRegister;
      maxima[kSecondOperand] = kIndexMax;
      break;
    default:
      return IndexedReadError::kUnsupportedOpcode;
  }
  if (instruction.operand_count != operand_count)
    return IndexedReadError::kMalformedInstruction;
  for (std::size_t i = 0; i < operand_count; ++i)
    if (instruction.operands[i].kind != kinds[i] ||
        instruction.operands[i].raw_bits > maxima[i])
      return IndexedReadError::kMalformedInstruction;
  ScalarSlot receiver = Accumulator{};
  if (kinds[kFirstOperand] == OperandKind::kRegister) {
    const auto reg = instruction.operands[kFirstOperand].raw_bits;
    if (reg >= frame_size) return IndexedReadError::kRegisterRange;
    receiver = abc::RegisterId{static_cast<std::uint32_t>(reg)};
  }
  return IndexedRead{receiver,
                     {static_cast<std::uint32_t>(
                         instruction.operands[index_operand].raw_bits)}};
}
}  // namespace abcd::analysis
