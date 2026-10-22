#include "abcd/analysis/define_instruction.h"
#include "abcd/analysis/own_write.h"

namespace abcd::analysis {
std::variant<OwnWrite, OwnWriteError> LiftOwnWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  OwnWriteKind kind;
  auto naming = PropertyNameSetting::kNone;
  std::uint64_t cache_max;
  switch (instruction.opcode) {
    case abc::Opcode::kSTOWNBYNAMEWITHNAMESET_IMM8_ID16_V8:
      naming = PropertyNameSetting::kPropertyKey;
      [[fallthrough]];
    case abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8:
      kind = OwnWriteKind::kStore;
      cache_max = UINT8_MAX;
      break;
    case abc::Opcode::kSTOWNBYNAMEWITHNAMESET_IMM16_ID16_V8:
      naming = PropertyNameSetting::kPropertyKey;
      [[fallthrough]];
    case abc::Opcode::kSTOWNBYNAME_IMM16_ID16_V8:
      kind = OwnWriteKind::kStore;
      cache_max = UINT16_MAX;
      break;
    case abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8:
    case abc::Opcode::kDEFINEPROPERTYBYNAME_IMM8_ID16_V8:
      kind = OwnWriteKind::kDefineField;
      cache_max = UINT8_MAX;
      break;
    default:
      return OwnWriteError::kUnsupportedOpcode;
  }
  // Pinned interpreter-inl.cpp STOWNBYNAME / DEFINEFIELDBYNAME handlers:
  // IC slot, string pool index, receiver register; value is accumulator.
  constexpr std::size_t kCache = 0, kName = 1, kObject = 2, kOperandCount = 3;
  if (instruction.operand_count != kOperandCount ||
      instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > cache_max ||
      instruction.operands[kName].kind != abc::OperandKind::kString ||
      instruction.operands[kName].raw_bits > UINT16_MAX ||
      instruction.operands[kObject].kind != abc::OperandKind::kRegister ||
      instruction.operands[kObject].raw_bits > UINT8_MAX)
    return OwnWriteError::kMalformedInstruction;
  if (instruction.operands[kObject].raw_bits >= frame_size)
    return OwnWriteError::kRegisterRange;
  return OwnWrite{
      kind,
      {static_cast<std::uint32_t>(instruction.operands[kName].raw_bits)},
      {static_cast<std::uint32_t>(instruction.operands[kObject].raw_bits)},
      static_cast<std::uint16_t>(instruction.operands[kCache].raw_bits), naming};
}
// Pinned v7.0 interpreter STOWNBYVALUE: IC slot, destination, key, acc value.
std::variant<ComputedOwnWrite, OwnWriteError> LiftComputedOwnWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  // DEFINEFIELD uses CreateDataPropertyOrThrow, including enumerable class
  // fields. Keep that identity distinct from STOWN's class non-enumerability.
  // OH7 interpreter-inl.cpp:7639; runtime_stubs-inl.h:3450.
  if(instruction.opcode==abc::Opcode::kCALLRUNTIME_DEFINEFIELDBYVALUE_PREF_IMM8_V8_V8){
    auto lifted=LiftDefineInstruction(instruction,frame_size);
    if(const auto* field=std::get_if<DefineFieldByValue>(&lifted))return ComputedOwnWrite{field->object,field->key,field->profile_slot,PropertyNameSetting::kNone,OwnWriteKind::kDefineField};
    if(const auto* error=std::get_if<DefineError>(&lifted))return *error==DefineError::kRegisterRange?OwnWriteError::kRegisterRange:OwnWriteError::kMalformedInstruction;
    return OwnWriteError::kMalformedInstruction;
  }
  std::uint64_t maximum;
  auto naming = PropertyNameSetting::kNone;
  switch (instruction.opcode) {
    case abc::Opcode::kSTOWNBYVALUEWITHNAMESET_IMM8_V8_V8:
      naming = PropertyNameSetting::kPropertyKey;
      [[fallthrough]];
    case abc::Opcode::kSTOWNBYVALUE_IMM8_V8_V8: maximum = UINT8_MAX; break;
    case abc::Opcode::kSTOWNBYVALUEWITHNAMESET_IMM16_V8_V8:
      naming = PropertyNameSetting::kPropertyKey;
      [[fallthrough]];
    case abc::Opcode::kSTOWNBYVALUE_IMM16_V8_V8: maximum = UINT16_MAX; break;
    default: return OwnWriteError::kUnsupportedOpcode;
  }
  constexpr std::size_t kCache = 0, kObject = 1, kKey = 2, kOperands = 3;
  if (instruction.operand_count != kOperands || instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > maximum) return OwnWriteError::kMalformedInstruction;
  for (const auto i : {kObject, kKey}) {
    const auto& operand = instruction.operands[i];
    if (operand.kind != abc::OperandKind::kRegister || operand.raw_bits > UINT8_MAX)
      return OwnWriteError::kMalformedInstruction;
    if (operand.raw_bits >= frame_size) return OwnWriteError::kRegisterRange;
  }
  return ComputedOwnWrite{{static_cast<std::uint32_t>(instruction.operands[kObject].raw_bits)},
      {static_cast<std::uint32_t>(instruction.operands[kKey].raw_bits)},
      static_cast<std::uint16_t>(instruction.operands[kCache].raw_bits), naming};
}
}  // namespace abcd::analysis
