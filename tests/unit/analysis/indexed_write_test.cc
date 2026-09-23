#include "abcd/analysis/indexed_write.h"

#include <array>
#include <cstdlib>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
}  // namespace

int main() {
  using namespace abcd;
  abc::DecodedInstruction instruction;
  instruction.opcode = abc::Opcode::kSTOWNBYINDEX_IMM8_V8_IMM16;
  instruction.operand_count = 3;
  instruction.operands[0] = {3, abc::OperandKind::kImmediate};
  instruction.operands[1] = {4, abc::OperandKind::kRegister};
  instruction.operands[2] = {201, abc::OperandKind::kImmediate};
  auto lifted = analysis::LiftIndexedWrite(instruction, 5);
  Check(std::get<analysis::IndexedWrite>(lifted).object.value == 4);
  Check(std::get<analysis::IndexedWrite>(lifted).index == 201);
  Check(std::get<analysis::IndexedWriteError>(
            analysis::LiftIndexedWrite(instruction, 4)) ==
        analysis::IndexedWriteError::kRegisterRange);
  const std::array<std::uint8_t, 5> encoded{0x79, 3, 4, 201, 0};
  const auto decoded = std::get<abc::DecodedInstruction>(
      abc::DecodeInstruction(encoded, abc::ProfileId::kDynamic1301));
  Check(std::get<analysis::IndexedWrite>(
            analysis::LiftIndexedWrite(decoded, 5)).index == 201);
  instruction.opcode = abc::Opcode::kSTOWNBYINDEX_IMM16_V8_IMM16;
  instruction.operands[0].raw_bits = UINT16_MAX;
  Check(std::holds_alternative<analysis::IndexedWrite>(
      analysis::LiftIndexedWrite(instruction, 5)));
  instruction.opcode = abc::Opcode::kWIDE_STOWNBYINDEX_PREF_V8_IMM32;
  instruction.operand_count = 2;
  instruction.operands[0] = {4, abc::OperandKind::kRegister};
  instruction.operands[1] = {UINT32_MAX, abc::OperandKind::kImmediate};
  Check(std::get<analysis::IndexedWrite>(
            analysis::LiftIndexedWrite(instruction, 5)).index == UINT32_MAX);
  instruction.operands[1].raw_bits = UINT64_MAX;
  Check(std::get<analysis::IndexedWriteError>(
            analysis::LiftIndexedWrite(instruction, 5)) ==
        analysis::IndexedWriteError::kMalformedInstruction);
}
