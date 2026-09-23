#include "abcd/analysis/class_creation.h"

#include <cstdlib>
int main() {
  using namespace abcd;
  const auto check = [](bool value) {
    if (!value) std::abort();
  };
  for (const bool wide : {false, true}) {
    std::vector<std::uint8_t> bytes{
        static_cast<std::uint8_t>(wide ? 0x75 : 0x35), 0xff};
    if (wide) bytes.push_back(0xff);
    bytes.insert(bytes.end(), {0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0xfe});
    const auto instruction = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(bytes, abc::ProfileId::kDynamic1206));
    const auto created = std::get<analysis::ClassCreation>(
        analysis::LiftClassCreation(instruction));
    check(created.constructor.value == 0x1234 &&
          created.literal.value == 0x5678);
    check(created.length == 0x9abc && created.parent.value == 0xfe);
    check(created.cache_slot == (wide ? UINT16_MAX : UINT8_MAX));
    for (std::size_t i = 0; i < instruction.operand_count; ++i) {
      auto bad = instruction;
      bad.operands[i].raw_bits = UINT64_MAX;
      check(std::get<analysis::CreationError>(analysis::LiftClassCreation(
                bad)) == analysis::CreationError::kMalformedInstruction);
    }
  }
  check(std::get<analysis::CreationError>(analysis::LiftClassCreation({})) ==
        analysis::CreationError::kUnsupportedOpcode);
}
