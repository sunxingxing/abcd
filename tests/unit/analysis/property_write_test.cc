#include "abcd/analysis/property_write.h"

#include <cstdlib>
#include <vector>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace
int main() {
  using namespace abcd;
  using namespace analysis;
  struct Case {
    std::uint8_t opcode;
    bool wide;
    PropertyWriteKind kind;
  };
  const Case cases[]{{0x43, false, PropertyWriteKind::kAssignment},
                     {0x91, true, PropertyWriteKind::kAssignment},
                     {0x7a, false, PropertyWriteKind::kOwnStore},
                     {0xcc, true, PropertyWriteKind::kOwnStore},
                     {0xdb, false, PropertyWriteKind::kDefineField},
                     {0xdc, false, PropertyWriteKind::kDefineField}};
  for (const auto& item : cases) {
    std::vector<std::uint8_t> bytes{item.opcode, 0xff};
    if (item.wide) bytes.push_back(0xff);
    bytes.insert(bytes.end(), {0xff, 0xff, 201});
    const auto decoded = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(bytes, abc::ProfileId::kDynamic1206));
    const auto write = std::get<PropertyWrite>(LiftPropertyWrite(decoded, 202));
    Check(write.kind == item.kind && write.name.value == UINT16_MAX &&
          write.object.value == 201);
    Check(std::get<PropertyWriteError>(LiftPropertyWrite(decoded, 201)) ==
          PropertyWriteError::kRegisterRange);
    for (std::size_t i = 0; i < decoded.operand_count; ++i) {
      auto bad = decoded;
      bad.operands[i].raw_bits = UINT64_MAX;
      Check(std::get<PropertyWriteError>(LiftPropertyWrite(bad, 202)) ==
            PropertyWriteError::kMalformedInstruction);
      bad = decoded;
      bad.operands[i].kind = abc::OperandKind::kLiteralArray;
      Check(std::get<PropertyWriteError>(LiftPropertyWrite(bad, 202)) ==
            PropertyWriteError::kMalformedInstruction);
    }
  }
  abc::DecodedInstruction other;
  other.opcode = abc::Opcode::kNOP;
  Check(std::get<PropertyWriteError>(LiftPropertyWrite(other, 0)) ==
        PropertyWriteError::kUnsupportedOpcode);
}
