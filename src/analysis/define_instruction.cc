#include "abcd/analysis/define_instruction.h"

#include <array>

namespace abcd::analysis {
namespace {
using Kind = abc::OperandKind;
constexpr std::size_t kDefineOperandSlots = 5;
constexpr std::uint8_t kFieldOperandCount = 3;
constexpr std::uint8_t kAccessorOperandCount = 4;
constexpr std::uint8_t kClassOperandCount = 5;
struct Format {
  std::uint8_t count;
  std::array<Kind, kDefineOperandSlots> kinds;
  std::array<std::uint64_t, kDefineOperandSlots> maxima;
};
constexpr std::uint64_t kByteMax = UINT8_MAX;
constexpr std::uint64_t kWordMax = UINT16_MAX;
constexpr std::uint64_t kDwordMax = UINT32_MAX;
constexpr Kind kUnused = Kind::kImmediate;
constexpr std::array<Kind, kDefineOperandSlots> kFieldValueKinds{
    Kind::kImmediate, Kind::kRegister, Kind::kRegister, kUnused, kUnused};
constexpr std::array<Kind, kDefineOperandSlots> kFieldIndexKinds{
    Kind::kImmediate, Kind::kImmediate, Kind::kRegister, kUnused, kUnused};
constexpr std::array<Kind, kDefineOperandSlots> kAccessorKinds{
    Kind::kRegister, Kind::kRegister, Kind::kRegister, Kind::kRegister,
    kUnused};
constexpr std::array<Kind, kDefineOperandSlots> kPrivateKinds{
    Kind::kImmediate, Kind::kImmediate, Kind::kImmediate, Kind::kRegister,
    kUnused};
constexpr std::array<Kind, kDefineOperandSlots> kSendableKinds{
    Kind::kImmediate, Kind::kMethod, Kind::kLiteralArray, Kind::kImmediate,
    Kind::kRegister};
constexpr std::array<Kind, kDefineOperandSlots> kLegacyKinds{
    Kind::kMethod, Kind::kImmediate, Kind::kImmediate, Kind::kRegister,
    Kind::kRegister};
}  // namespace

DefineInstruction LiftDefineInstruction(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  Format format{};
  using abc::Opcode;
  switch (instruction.opcode) {
    case Opcode::kCALLRUNTIME_DEFINEFIELDBYVALUE_PREF_IMM8_V8_V8:
      format = {kFieldOperandCount, kFieldValueKinds,
                {kByteMax, kByteMax, kByteMax, 0, 0}};
      break;
    case Opcode::kCALLRUNTIME_DEFINEFIELDBYINDEX_PREF_IMM8_IMM32_V8:
      format = {kFieldOperandCount, kFieldIndexKinds,
                {kByteMax, kDwordMax, kByteMax, 0, 0}};
      break;
    case Opcode::kDEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8:
      format = {kAccessorOperandCount, kAccessorKinds,
                {kByteMax, kByteMax, kByteMax, kByteMax, 0}};
      break;
    case Opcode::kCALLRUNTIME_DEFINEPRIVATEPROPERTY_PREF_IMM8_IMM16_IMM16_V8:
      format = {kAccessorOperandCount, kPrivateKinds,
                {kByteMax, kWordMax, kWordMax, kByteMax, 0}};
      break;
    case Opcode::kCALLRUNTIME_DEFINESENDABLECLASS_PREF_IMM16_ID16_ID16_IMM16_V8:
      format = {kClassOperandCount, kSendableKinds,
                {kWordMax, kWordMax, kWordMax, kWordMax, kByteMax}};
      break;
    case Opcode::kDEPRECATED_DEFINECLASSWITHBUFFER_PREF_ID16_IMM16_IMM16_V8_V8:
      format = {kClassOperandCount, kLegacyKinds,
                {kWordMax, kWordMax, kWordMax, kByteMax, kByteMax}};
      break;
    default:
      return DefineError::kUnsupportedOpcode;
  }
  if (instruction.operand_count != format.count)
    return DefineError::kMalformedInstruction;
  for (std::size_t i = 0; i < format.count; ++i)
    if (instruction.operands[i].kind != format.kinds[i] ||
        instruction.operands[i].raw_bits > format.maxima[i])
      return DefineError::kMalformedInstruction;
  const auto raw = [&](std::size_t i) { return instruction.operands[i].raw_bits; };
  for (std::size_t i = 0; i < format.count; ++i)
    if (format.kinds[i] == Kind::kRegister && raw(i) >= frame_size)
      return DefineError::kRegisterRange;
  const auto reg = [&](std::size_t i) {
    return abc::RegisterId{static_cast<std::uint32_t>(raw(i))};
  };
  const auto pool = [&](std::size_t i) {
    return abc::PoolIndex{static_cast<std::uint32_t>(raw(i))};
  };
  switch (instruction.opcode) {
    case Opcode::kCALLRUNTIME_DEFINEFIELDBYVALUE_PREF_IMM8_V8_V8:
      return DefineFieldByValue{static_cast<std::uint8_t>(raw(0)),
                                reg(1), reg(2)};
    case Opcode::kCALLRUNTIME_DEFINEFIELDBYINDEX_PREF_IMM8_IMM32_V8:
      return DefineFieldByIndex{static_cast<std::uint8_t>(raw(0)),
                                static_cast<std::uint32_t>(raw(1)), reg(2)};
    case Opcode::kDEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8:
      return DefineAccessor{reg(0), reg(1), reg(2), reg(3)};
    case Opcode::kCALLRUNTIME_DEFINEPRIVATEPROPERTY_PREF_IMM8_IMM16_IMM16_V8:
      return DefinePrivateProperty{static_cast<std::uint8_t>(raw(0)),
                                   static_cast<std::uint16_t>(raw(1)),
                                   static_cast<std::uint16_t>(raw(2)), reg(3)};
    case Opcode::kCALLRUNTIME_DEFINESENDABLECLASS_PREF_IMM16_ID16_ID16_IMM16_V8:
      return DefineSendableClass{static_cast<std::uint16_t>(raw(0)),
                                 pool(1), pool(2),
                                 static_cast<std::uint16_t>(raw(3)), reg(4)};
    case Opcode::kDEPRECATED_DEFINECLASSWITHBUFFER_PREF_ID16_IMM16_IMM16_V8_V8:
      return DefineLegacyClass{pool(0), static_cast<std::uint16_t>(raw(1)),
                               static_cast<std::uint16_t>(raw(2)),
                               reg(3), reg(4)};
    default:
      return DefineError::kUnsupportedOpcode;
  }
}
}  // namespace abcd::analysis
