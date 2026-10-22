#include "abcd/analysis/this_property.h"

namespace abcd::analysis {
std::variant<ThisProperty, ThisPropertyError> LiftThisProperty(
    const abc::DecodedInstruction& instruction, std::uint64_t frame) {
  using O = abc::Opcode;
  using K = ThisPropertyKind;
  using E = ThisPropertyError;
  K kind;
  std::uint64_t maximum;
  switch (instruction.opcode) {
    case O::kLDTHISBYNAME_IMM8_ID16: kind = K::kReadName; maximum = UINT8_MAX; break;
    case O::kLDTHISBYNAME_IMM16_ID16: kind = K::kReadName; maximum = UINT16_MAX; break;
    case O::kSTTHISBYNAME_IMM8_ID16: kind = K::kWriteName; maximum = UINT8_MAX; break;
    case O::kSTTHISBYNAME_IMM16_ID16: kind = K::kWriteName; maximum = UINT16_MAX; break;
    case O::kLDTHISBYVALUE_IMM8: kind = K::kReadValue; maximum = UINT8_MAX; break;
    case O::kLDTHISBYVALUE_IMM16: kind = K::kReadValue; maximum = UINT16_MAX; break;
    case O::kSTTHISBYVALUE_IMM8_V8: kind = K::kWriteValue; maximum = UINT8_MAX; break;
    case O::kSTTHISBYVALUE_IMM16_V8: kind = K::kWriteValue; maximum = UINT16_MAX; break;
    default: return E::kUnsupportedOpcode;
  }
  const bool named = NamedThisProperty(kind), write = WritesThisProperty(kind);
  if (instruction.operand_count != (named || write ? 2 : 1) ||
      instruction.operands[0].kind != abc::OperandKind::kImmediate ||
      instruction.operands[0].raw_bits > maximum) return E::kMalformedInstruction;
  ThisProperty result{kind, {}, {}};
  if (named) {
    const auto& name = instruction.operands[1];
    if (name.kind != abc::OperandKind::kString || name.raw_bits > UINT16_MAX)
      return E::kMalformedInstruction;
    result.name = abc::PoolIndex{static_cast<std::uint32_t>(name.raw_bits)};
  } else if (write) {
    const auto& key = instruction.operands[1];
    if (key.kind != abc::OperandKind::kRegister || key.raw_bits > UINT8_MAX)
      return E::kMalformedInstruction;
    if (key.raw_bits >= frame) return E::kRegisterRange;
    result.key = abc::RegisterId{static_cast<std::uint32_t>(key.raw_bits)};
  }
  return result;
}
}  // namespace abcd::analysis
