#include "abcd/analysis/indexed_read.h"
#include "abcd/analysis/scalar.h"
#include <bit>

#include <array>
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
  const std::array encodings{Decode({0x3a, 0xff, 0xff, 0xff}),
                             Decode({0x88, 0xff, 0xff, 0xff, 0xff}),
      Decode({0xfd, 8, 0xff, 0xff, 0xff, 0xff}),
      Decode({0xfc, 29, 0xff, 0xff, 0xff, 0xff, 0xff})};
  const std::array opcodes{abc::Opcode::kLDOBJBYINDEX_IMM8_IMM16,
                          abc::Opcode::kLDOBJBYINDEX_IMM16_IMM16,
                          abc::Opcode::kWIDE_LDOBJBYINDEX_PREF_IMM32,
                          abc::Opcode::kDEPRECATED_LDOBJBYINDEX_PREF_V8_IMM32};
  for (std::size_t form = 0; form < encodings.size(); ++form) {
    const bool legacy = form == 3;
    const auto decoded = encodings[form];
    Check(decoded.opcode == opcodes[form]);
    const auto read = std::get<IndexedRead>(LiftIndexedRead(decoded, 256));
    Check(read.index.value == (form < 2 ? UINT16_MAX : UINT32_MAX));
    if (legacy)
      Check(std::get<abc::RegisterId>(read.receiver).value == 255);
    else
      Check(std::holds_alternative<Accumulator>(read.receiver));
    const std::size_t index_operand = form == 2 ? 0 : 1;
    for (const std::uint32_t index :
         {0U, 200U, 201U, 65535U, 2147483648U, UINT32_MAX}) {
      if (form < 2 && index > UINT16_MAX) continue;
      auto instruction = decoded;
      instruction.operands[index_operand].raw_bits = index;
      const auto scalar = std::get<ScalarAssignment>(LiftScalarInstruction(instruction, 256));
      Check(scalar.completion == CompletionEffect::kMayThrow);
      const auto property = std::get<PropertyRead>(scalar.expression);
      Check(std::bit_cast<double>(std::get<Float64Bits>(property.key).value) ==
            static_cast<double>(index));
      Check(legacy ? std::get<abc::RegisterId>(property.receiver).value == 255
                   : std::holds_alternative<Accumulator>(property.receiver));
      Check(std::get<IndexedRead>(LiftIndexedRead(instruction, 256))
                .index.value == index);
    }
    auto malformed = decoded;
    malformed.operand_count = 7;
    Check(std::get<IndexedReadError>(LiftIndexedRead(malformed, 256)) ==
          IndexedReadError::kMalformedInstruction);
    for (std::size_t operand = 0; operand < decoded.operand_count; ++operand) {
      malformed = decoded;
      malformed.operands[operand].raw_bits = UINT64_MAX;
      Check(
          std::get<IndexedReadError>(LiftIndexedRead(malformed, UINT64_MAX)) ==
          IndexedReadError::kMalformedInstruction);
      malformed = decoded;
      malformed.operands[operand].kind = abc::OperandKind::kString;
      Check(std::get<IndexedReadError>(LiftIndexedRead(malformed, 256)) ==
            IndexedReadError::kMalformedInstruction);
    }
  }
  Check(std::get<IndexedReadError>(LiftIndexedRead(encodings.back(), 255)) ==
        IndexedReadError::kRegisterRange);
  Check(std::holds_alternative<IndexedRead>(
      LiftIndexedRead(encodings.front(), 0)));
  Check(std::get<IndexedReadError>(LiftIndexedRead(Decode({0}), 0)) ==
        IndexedReadError::kUnsupportedOpcode);
}
