#include "abcd/analysis/named_read.h"

#include <cstdlib>
#include <vector>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
abcd::abc::DecodedInstruction Decode(
    std::initializer_list<std::uint8_t> bytes) {
  const std::vector<std::uint8_t> storage(bytes);
  return std::get<abcd::abc::DecodedInstruction>(abcd::abc::DecodeInstruction(
      storage, abcd::abc::ProfileId::kDynamic1206));
}
}  // namespace
int main() {
  using namespace abcd;
  using namespace analysis;
  for (const auto& decoded : {Decode({0x42, 0xff, 0xff, 0xff}),
                             Decode({0x90, 0xff, 0xff, 0xff, 0xff})}) {
    Check(decoded.opcode == abc::Opcode::kLDOBJBYNAME_IMM8_ID16 ||
          decoded.opcode == abc::Opcode::kLDOBJBYNAME_IMM16_ID16);
    for (const auto slot : {0U, 200U, 201U, 65535U}) {
      auto instruction = decoded;
      instruction.operands[1].raw_bits = slot;
      Check(std::get<NamedRead>(LiftNamedRead(instruction)).name.value == slot);
    }
    auto malformed = decoded;
    malformed.operand_count = 7;
    Check(std::get<NamedReadError>(LiftNamedRead(malformed)) ==
          NamedReadError::kMalformedInstruction);
    for (const auto operand : {0U, 1U}) {
      malformed = decoded;
      malformed.operands[operand].raw_bits = 65536;
      Check(std::get<NamedReadError>(LiftNamedRead(malformed)) ==
            NamedReadError::kMalformedInstruction);
      malformed = decoded;
      malformed.operands[operand].kind = abc::OperandKind::kRegister;
      Check(std::get<NamedReadError>(LiftNamedRead(malformed)) ==
            NamedReadError::kMalformedInstruction);
    }
  }
  auto narrow = Decode({0x42, 0xff, 0, 0});
  narrow.operands[0].raw_bits = 256;
  Check(std::get<NamedReadError>(LiftNamedRead(narrow)) ==
        NamedReadError::kMalformedInstruction);
  const auto legacy = Decode({0xfc, 37, 0, 0, 1, 0, 0});
  Check(legacy.opcode == abc::Opcode::kDEPRECATED_LDOBJBYNAME_PREF_ID32_V8);
  Check(std::get<NamedReadError>(LiftNamedRead(legacy)) ==
        NamedReadError::kUnsupportedOpcode);
}
