#include "abcd/analysis/iterator.h"

#include <climits>

namespace abcd::analysis {
namespace {
constexpr std::size_t kCache = 0;
constexpr std::size_t kIterator = 0;
constexpr std::size_t kCloseIterator = 1;
constexpr std::size_t kNextMethod = 1;

bool Immediate(const abc::DecodedInstruction& instruction, std::size_t index,
               std::uint64_t maximum) {
  return instruction.operands[index].kind == abc::OperandKind::kImmediate &&
         instruction.operands[index].raw_bits <= maximum;
}
bool Register(const abc::DecodedInstruction& instruction, std::size_t index) {
  return instruction.operands[index].kind == abc::OperandKind::kRegister &&
         instruction.operands[index].raw_bits <= UINT8_MAX;
}
}  // namespace

IteratorInstruction LiftIteratorInstruction(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  using abc::Opcode;
  switch (instruction.opcode) {
    case Opcode::kGETPROPITERATOR:
      if (instruction.operand_count != 0)
        return IteratorError::kMalformedInstruction;
      return PropertyIterator{};
    case Opcode::kGETITERATOR_IMM8:
    case Opcode::kGETITERATOR_IMM16:
    case Opcode::kGETASYNCITERATOR_IMM8: {
      const bool wide = instruction.opcode == Opcode::kGETITERATOR_IMM16;
      if (instruction.operand_count != 1 ||
          !Immediate(instruction, kCache, wide ? UINT16_MAX : UINT8_MAX))
        return IteratorError::kMalformedInstruction;
      return AcquireIterator{
          instruction.opcode == Opcode::kGETASYNCITERATOR_IMM8
              ? IteratorMode::kAsync : IteratorMode::kSync,
          static_cast<std::uint16_t>(instruction.operands[kCache].raw_bits)};
    }
    case Opcode::kCLOSEITERATOR_IMM8_V8:
    case Opcode::kCLOSEITERATOR_IMM16_V8: {
      const bool wide = instruction.opcode == Opcode::kCLOSEITERATOR_IMM16_V8;
      if (instruction.operand_count != 2 ||
          !Immediate(instruction, kCache, wide ? UINT16_MAX : UINT8_MAX) ||
          !Register(instruction, kCloseIterator))
        return IteratorError::kMalformedInstruction;
      const auto reg = instruction.operands[kCloseIterator].raw_bits;
      if (reg >= frame_size) return IteratorError::kRegisterRange;
      return CloseIterator{
          static_cast<std::uint16_t>(instruction.operands[kCache].raw_bits),
          abc::RegisterId{static_cast<std::uint32_t>(reg)}};
    }
    case Opcode::kDEPRECATED_GETITERATORNEXT_PREF_V8_V8: {
      if (instruction.operand_count != 2 ||
          !Register(instruction, kIterator) ||
          !Register(instruction, kNextMethod))
        return IteratorError::kMalformedInstruction;
      const auto iterator = instruction.operands[kIterator].raw_bits;
      const auto method = instruction.operands[kNextMethod].raw_bits;
      if (iterator >= frame_size || method >= frame_size)
        return IteratorError::kRegisterRange;
      return IteratorNext{
          abc::RegisterId{static_cast<std::uint32_t>(iterator)},
          abc::RegisterId{static_cast<std::uint32_t>(method)}};
    }
    default:
      return IteratorError::kUnsupportedOpcode;
  }
}
}  // namespace abcd::analysis
