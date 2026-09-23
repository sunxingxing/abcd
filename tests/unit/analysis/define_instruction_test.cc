#include "abcd/analysis/define_instruction.h"

#include <cstdlib>
#include <vector>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
abcd::abc::DecodedInstruction Instruction(
    abcd::abc::Opcode opcode,
    std::initializer_list<abcd::abc::OperandKind> kinds) {
  abcd::abc::DecodedInstruction instruction;
  instruction.opcode = opcode;
  instruction.operand_count = static_cast<std::uint8_t>(kinds.size());
  std::size_t i = 0;
  for (const auto kind : kinds) {
    instruction.operands[i].kind = kind;
    instruction.operands[i].raw_bits = i + 1;
    ++i;
  }
  return instruction;
}
}  // namespace

int main() {
  using namespace abcd;
  using enum abc::OperandKind;
  using analysis::DefineError;
  using analysis::LiftDefineInstruction;
  const auto check = [&](abc::DecodedInstruction instruction,
                         auto expected) {
    const auto lifted = LiftDefineInstruction(instruction, 16);
    Check(std::holds_alternative<decltype(expected)>(lifted));
    for (std::size_t i = 0; i < instruction.operand_count; ++i) {
      auto bad = instruction;
      bad.operands[i].raw_bits = UINT64_MAX;
      Check(std::get<DefineError>(LiftDefineInstruction(bad, 16)) ==
            DefineError::kMalformedInstruction);
      bad = instruction;
      bad.operands[i].kind = kString;
      Check(std::get<DefineError>(LiftDefineInstruction(bad, 16)) ==
            DefineError::kMalformedInstruction);
    }
    instruction.operand_count = 0;
    Check(std::get<DefineError>(LiftDefineInstruction(instruction, 16)) ==
          DefineError::kMalformedInstruction);
  };
  check(Instruction(abc::Opcode::kCALLRUNTIME_DEFINEFIELDBYVALUE_PREF_IMM8_V8_V8,
                    {kImmediate, kRegister, kRegister}),
        analysis::DefineFieldByValue{});
  check(Instruction(abc::Opcode::kCALLRUNTIME_DEFINEFIELDBYINDEX_PREF_IMM8_IMM32_V8,
                    {kImmediate, kImmediate, kRegister}),
        analysis::DefineFieldByIndex{});
  check(Instruction(abc::Opcode::kDEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8,
                    {kRegister, kRegister, kRegister, kRegister}),
        analysis::DefineAccessor{});
  check(Instruction(abc::Opcode::kCALLRUNTIME_DEFINEPRIVATEPROPERTY_PREF_IMM8_IMM16_IMM16_V8,
                    {kImmediate, kImmediate, kImmediate, kRegister}),
        analysis::DefinePrivateProperty{});
  check(Instruction(abc::Opcode::kCALLRUNTIME_DEFINESENDABLECLASS_PREF_IMM16_ID16_ID16_IMM16_V8,
                    {kImmediate, kMethod, kLiteralArray, kImmediate, kRegister}),
        analysis::DefineSendableClass{});
  check(Instruction(abc::Opcode::kDEPRECATED_DEFINECLASSWITHBUFFER_PREF_ID16_IMM16_IMM16_V8_V8,
                    {kMethod, kImmediate, kImmediate, kRegister, kRegister}),
        analysis::DefineLegacyClass{});

  const auto by_value = Instruction(
      abc::Opcode::kCALLRUNTIME_DEFINEFIELDBYVALUE_PREF_IMM8_V8_V8,
      {kImmediate, kRegister, kRegister});
  const auto lifted = std::get<analysis::DefineFieldByValue>(
      LiftDefineInstruction(by_value, 16));
  Check(lifted.key.value == 2 && lifted.object.value == 3);
  Check(lifted.profile_slot == 1);
  Check(std::get<DefineError>(LiftDefineInstruction(by_value, 3)) ==
        DefineError::kRegisterRange);
  const auto by_index = std::get<analysis::DefineFieldByIndex>(
      LiftDefineInstruction(Instruction(
          abc::Opcode::kCALLRUNTIME_DEFINEFIELDBYINDEX_PREF_IMM8_IMM32_V8,
          {kImmediate, kImmediate, kRegister}), 16));
  Check(by_index.profile_slot == 1 && by_index.index == 2 &&
        by_index.object.value == 3);
  const std::vector<std::vector<std::uint8_t>> encodings{
      {0xbc, 1, 2, 3, 4},
      {0xfb, 1, 7, 1, 2},
      {0xfb, 2, 7, 0x78, 0x56, 0x34, 0x12, 2},
      {0xfb, 5, 7, 0x34, 0x12, 0x78, 0x56, 2},
      {0xfb, 7, 1, 0, 2, 0, 3, 0, 4, 0, 5},
      {0xfc, 0x12, 1, 0, 2, 0, 3, 0, 4, 5}};
  for (const auto& bytes : encodings) {
    const auto decoded = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(bytes, abc::ProfileId::kDynamic1301));
    Check(!std::holds_alternative<DefineError>(
        LiftDefineInstruction(decoded, 16)));
    Check(decoded.size == bytes.size());
  }
  Check(std::get<DefineError>(LiftDefineInstruction({}, 16)) ==
        DefineError::kUnsupportedOpcode);
}
