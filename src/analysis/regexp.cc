#include "abcd/analysis/regexp.h"
namespace abcd::analysis {
std::variant<RegExpLiteral, ScalarError> LiftRegExpLiteral(
    const abc::DecodedInstruction& d) {
  using O = abc::Opcode;
  using K = abc::OperandKind;
  const bool wide = d.opcode == O::kCREATEREGEXPWITHLITERAL_IMM16_ID16_IMM8;
  if (!wide && d.opcode != O::kCREATEREGEXPWITHLITERAL_IMM8_ID16_IMM8)
    return ScalarError::kUnsupportedOpcode;
  if (d.operand_count != 3 || d.operands[0].kind != K::kImmediate ||
      d.operands[0].raw_bits > (wide ? UINT16_MAX : UINT8_MAX) ||
      d.operands[1].kind != K::kString || d.operands[1].raw_bits > UINT16_MAX ||
      d.operands[2].kind != K::kImmediate ||
      d.operands[2].raw_bits > kRegExpFlagMask)
    return ScalarError::kMalformedInstruction;
  return RegExpLiteral{{static_cast<std::uint16_t>(d.operands[1].raw_bits)},
                       {static_cast<std::uint8_t>(d.operands[2].raw_bits)}};
}
}  // namespace abcd::analysis
