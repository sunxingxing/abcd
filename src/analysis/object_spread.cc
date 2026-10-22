#include "abcd/analysis/object_spread.h"

namespace abcd::analysis {
std::variant<CopyProperties, ScalarError> LiftCopyProperties(
    const abc::DecodedInstruction& instruction, std::uint64_t frame) {
  using Opcode = abc::Opcode;
  const bool legacy = instruction.opcode == Opcode::kDEPRECATED_COPYDATAPROPERTIES_PREF_V8_V8;
  if (instruction.opcode != Opcode::kCOPYDATAPROPERTIES_V8 && !legacy)
    return ScalarError::kUnsupportedOpcode;
  if (instruction.operand_count != (legacy ? 2 : 1))
    return ScalarError::kMalformedInstruction;
  for (std::uint8_t i = 0; i < instruction.operand_count; ++i) {
    const auto& operand = instruction.operands[i];
    if (operand.kind != abc::OperandKind::kRegister || operand.raw_bits > UINT8_MAX)
      return ScalarError::kMalformedInstruction;
    if (operand.raw_bits >= frame) return ScalarError::kRegisterRange;
  }
  const abc::RegisterId object{static_cast<std::uint32_t>(instruction.operands[0].raw_bits)};
  const ScalarValue source = legacy
      ? ScalarValue{abc::RegisterId{static_cast<std::uint32_t>(instruction.operands[1].raw_bits)}}
      : ScalarValue{Accumulator{}};
  return CopyProperties{object, source};
}
}  // namespace abcd::analysis
