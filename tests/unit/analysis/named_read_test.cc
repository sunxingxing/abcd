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
      Check(std::get<NamedRead>(LiftNamedRead(instruction, 1)).name.value == slot);
    }
    auto malformed = decoded;
    malformed.operand_count = 7;
    Check(std::get<NamedReadError>(LiftNamedRead(malformed, 1)) ==
          NamedReadError::kMalformedInstruction);
    for (const auto operand : {0U, 1U}) {
      malformed = decoded;
      malformed.operands[operand].raw_bits = 65536;
      Check(std::get<NamedReadError>(LiftNamedRead(malformed, 1)) ==
            NamedReadError::kMalformedInstruction);
      malformed = decoded;
      malformed.operands[operand].kind = abc::OperandKind::kRegister;
      Check(std::get<NamedReadError>(LiftNamedRead(malformed, 1)) ==
            NamedReadError::kMalformedInstruction);
    }
  }
  auto narrow = Decode({0x42, 0xff, 0, 0});
  narrow.operands[0].raw_bits = 256;
  Check(std::get<NamedReadError>(LiftNamedRead(narrow, 1)) ==
        NamedReadError::kMalformedInstruction);
  const auto legacy = Decode({0xfc, 37, 0, 0, 1, 0, 0});
  Check(legacy.opcode == abc::Opcode::kDEPRECATED_LDOBJBYNAME_PREF_ID32_V8);
  // The pinned runtime narrows ID32 to uint16; 0x10000 would alias slot 0.
  Check(std::get<NamedReadError>(LiftNamedRead(legacy, 1)) ==
        NamedReadError::kMalformedInstruction);
  const auto register_receiver = Decode({0xfc, 37, 7, 0, 0, 0, 2});
  const auto read = std::get<NamedRead>(LiftNamedRead(register_receiver, 3));
  Check(read.name.value == 7 && read.receiver && read.receiver->value == 2);
  Check(std::get<NamedReadError>(LiftNamedRead(register_receiver, 2)) ==
        NamedReadError::kMalformedInstruction);
}
