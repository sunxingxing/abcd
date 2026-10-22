#include "abcd/analysis/object_spread.h"

namespace abcd::analysis {
std::variant<CopyProperties, ScalarError> LiftCopyProperties(
    const abc::DecodedInstruction& instruction, std::uint64_t frame) {
  using Opcode = abc::Opcode;
  // OH7 interpreter-inl.cpp:5489-5513: acc is the target, v8 the
  // prototype. Acc is preserved on Commit; the runtime ignores primitives.
  if (instruction.opcode == Opcode::kSETOBJECTWITHPROTO_IMM8_V8 ||
      instruction.opcode == Opcode::kSETOBJECTWITHPROTO_IMM16_V8) {
    const auto maximum = instruction.opcode == Opcode::kSETOBJECTWITHPROTO_IMM8_V8 ? UINT8_MAX : UINT16_MAX;
    if (instruction.operand_count != 2 ||
        instruction.operands[0].kind != abc::OperandKind::kImmediate ||
        instruction.operands[0].raw_bits > maximum ||
        instruction.operands[1].kind != abc::OperandKind::kRegister ||
        instruction.operands[1].raw_bits > UINT8_MAX)
      return ScalarError::kMalformedInstruction;
    if (instruction.operands[1].raw_bits >= frame) return ScalarError::kRegisterRange;
    return CopyProperties{Accumulator{},
        abc::RegisterId{static_cast<std::uint32_t>(instruction.operands[1].raw_bits)},
        ObjectMutationKind::kSetPrototype};
  }
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
