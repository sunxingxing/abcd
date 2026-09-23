#include "abcd/analysis/private_property.h"

#include <climits>

namespace abcd::analysis {
namespace {
constexpr std::size_t kCache = 0;
constexpr std::size_t kLevel = 1;
constexpr std::size_t kSlot = 2;
constexpr std::size_t kObject = 3;
}  // namespace

PrivatePropertyResult LiftPrivateProperty(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  using abc::Opcode;
  bool store = false;
  switch (instruction.opcode) {
    case Opcode::kLDPRIVATEPROPERTY_IMM8_IMM16_IMM16:
      break;
    case Opcode::kSTPRIVATEPROPERTY_IMM8_IMM16_IMM16_V8:
      store = true;
      break;
    case Opcode::kTESTIN_IMM8_IMM16_IMM16:
      break;
    default:
      return PrivatePropertyError::kUnsupportedOpcode;
  }
  if (instruction.operand_count !=
      (store ? kObject + 1 : kSlot + 1))
    return PrivatePropertyError::kMalformedInstruction;
  if (instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > UINT8_MAX ||
      instruction.operands[kLevel].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kLevel].raw_bits > UINT16_MAX ||
      instruction.operands[kSlot].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kSlot].raw_bits > UINT16_MAX)
    return PrivatePropertyError::kMalformedInstruction;
  const PrivateNameRef name{
      static_cast<std::uint8_t>(instruction.operands[kCache].raw_bits),
      static_cast<std::uint16_t>(instruction.operands[kLevel].raw_bits),
      static_cast<std::uint16_t>(instruction.operands[kSlot].raw_bits)};
  if (store) {
    if (instruction.operands[kObject].kind != abc::OperandKind::kRegister ||
        instruction.operands[kObject].raw_bits > UINT8_MAX)
      return PrivatePropertyError::kMalformedInstruction;
    if (instruction.operands[kObject].raw_bits >= frame_size)
      return PrivatePropertyError::kRegisterRange;
    return PrivateStore{name, abc::RegisterId{static_cast<std::uint32_t>(
                                  instruction.operands[kObject].raw_bits)}};
  }
  if (instruction.opcode == Opcode::kTESTIN_IMM8_IMM16_IMM16)
    return PrivateHas{name};
  return PrivateLoad{name};
}
}  // namespace abcd::analysis
