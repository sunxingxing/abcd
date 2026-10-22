#include "abcd/analysis/object_rest.h"
namespace abcd::analysis {
std::variant<ObjectRest, ScalarError> LiftObjectRest(
    const abc::DecodedInstruction& d, std::uint64_t frame) {
  using O=abc::Opcode;using K=abc::OperandKind;
  const bool wide=d.opcode==O::kWIDE_CREATEOBJECTWITHEXCLUDEDKEYS_PREF_IMM16_V8_V8;
  if(!wide && d.opcode!=O::kCREATEOBJECTWITHEXCLUDEDKEYS_IMM8_V8_V8)
    return ScalarError::kUnsupportedOpcode;
  if(d.operand_count!=3 || d.operands[0].kind!=K::kImmediate ||
      d.operands[0].raw_bits>(wide?UINT16_MAX:UINT8_MAX))return ScalarError::kMalformedInstruction;
  for(unsigned i=1;i<3;++i)
    if(d.operands[i].kind!=K::kRegister || d.operands[i].raw_bits>UINT8_MAX)
      return ScalarError::kMalformedInstruction;
  const auto count=static_cast<std::uint32_t>(d.operands[0].raw_bits)+1;
  const auto base=static_cast<std::uint32_t>(d.operands[1].raw_bits);
  const auto first=static_cast<std::uint32_t>(d.operands[2].raw_bits);
  if(base>=frame || first>=frame || count>frame-first)return ScalarError::kRegisterRange;
  return ObjectRest{{base},{first},count};
}
}  // namespace abcd::analysis
