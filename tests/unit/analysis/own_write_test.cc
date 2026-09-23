#include "abcd/analysis/own_write.h"

#include <cstdlib>
#include <vector>
namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace
int main() {
  using namespace abcd;
  for (const auto opcode : {0x7a, 0xcc, 0xdb, 0xdc}) {
    const bool wide = opcode == 0xcc;
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(opcode), 0xff};
    if (wide) bytes.push_back(0xff);
    bytes.insert(bytes.end(), {0xff, 0xff, 201});
    const auto decoded = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(bytes, abc::ProfileId::kDynamic1206));
    const auto write =
        std::get<analysis::OwnWrite>(analysis::LiftOwnWrite(decoded, 202));
    Check(write.name.value == 65535 && write.object.value == 201);
    Check(write.cache_slot == (wide ? UINT16_MAX : UINT8_MAX));
    Check(write.kind == (opcode == 0xdb || opcode == 0xdc
                             ? analysis::OwnWriteKind::kDefineField
                                        : analysis::OwnWriteKind::kStore));
    Check(std::get<analysis::OwnWriteError>(analysis::LiftOwnWrite(
              decoded, 201)) == analysis::OwnWriteError::kRegisterRange);
    for (std::size_t i = 0; i < decoded.operand_count; ++i) {
      auto bad = decoded;
      bad.operands[i].raw_bits = UINT64_MAX;
      Check(std::get<analysis::OwnWriteError>(
                analysis::LiftOwnWrite(bad, UINT64_MAX)) ==
            analysis::OwnWriteError::kMalformedInstruction);
      bad = decoded;
      bad.operands[i].kind = abc::OperandKind::kLiteralArray;
      Check(std::get<analysis::OwnWriteError>(analysis::LiftOwnWrite(
                bad, 202)) == analysis::OwnWriteError::kMalformedInstruction);
    }
  }
  for (const auto opcode : {abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8,
                            abc::Opcode::kSTTHISBYNAME_IMM8_ID16}) {
    abc::DecodedInstruction instruction;
    instruction.opcode = opcode;
    Check(std::get<analysis::OwnWriteError>(analysis::LiftOwnWrite(
              instruction, 1)) == analysis::OwnWriteError::kUnsupportedOpcode);
  }
}
