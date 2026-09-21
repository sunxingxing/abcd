#include "abcd/abc/instruction.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <variant>

namespace {

struct EncodingCase {
  std::uint16_t opcode;
  std::uint8_t size;
};

#include "abcd/abc/generated_isa_cases.inc"

void Check(bool condition) {
  if (!condition) std::abort();
}

template <std::size_t N>
abcd::abc::InstructionResult Decode(const std::array<std::uint8_t, N>& bytes) {
  return abcd::abc::DecodeInstruction(bytes,
                                      abcd::abc::ProfileId::kDynamic1206);
}

}  // namespace

int main() {
  using abcd::abc::DecodedInstruction;
  using abcd::abc::InstructionError;
  using abcd::abc::OperandKind;
  const auto no_operands = Decode(std::array<std::uint8_t, 1>{0x00});
  Check(std::holds_alternative<DecodedInstruction>(no_operands));
  Check(std::get<DecodedInstruction>(no_operands).size == 1);
  Check(std::get<DecodedInstruction>(no_operands).operand_count == 0);

  const auto nibble = Decode(std::array<std::uint8_t, 2>{0x44, 0xa3});
  Check(std::holds_alternative<DecodedInstruction>(nibble));
  const auto& move = std::get<DecodedInstruction>(nibble);
  Check(move.size == 2 && move.operand_count == 2);
  Check(move.operands[0].kind == OperandKind::kRegister);
  Check(move.operands[0].raw_bits == 3);
  Check(move.operands[1].raw_bits == 10);

  const auto branch = Decode(std::array<std::uint8_t, 2>{0x4d, 0xff});
  Check(std::holds_alternative<DecodedInstruction>(branch));
  Check(std::get<DecodedInstruction>(branch).branch_delta == -1);

  const auto wide = Decode(std::array<std::uint8_t, 4>{0xfd, 0x02, 0x34, 0x12});
  Check(std::holds_alternative<DecodedInstruction>(wide));
  Check(std::get<DecodedInstruction>(wide).size == 4);
  Check(std::get<DecodedInstruction>(wide).operands[0].raw_bits == 0x1234);

  const auto thrown = Decode(std::array<std::uint8_t, 2>{0xfe, 0x00});
  Check(std::holds_alternative<DecodedInstruction>(thrown));
  Check(std::get<DecodedInstruction>(thrown).size == 2);
  const auto deprecated = Decode(std::array<std::uint8_t, 2>{0xfc, 0x00});
  Check(std::holds_alternative<DecodedInstruction>(deprecated));
  const auto runtime = Decode(std::array<std::uint8_t, 2>{0xfb, 0x00});
  Check(std::holds_alternative<DecodedInstruction>(runtime));

  const auto branch16 = Decode(std::array<std::uint8_t, 3>{0x4e, 0xfe, 0xff});
  Check(std::get<DecodedInstruction>(branch16).branch_delta == -2);
  const auto branch32 =
      Decode(std::array<std::uint8_t, 5>{0x98, 0xfc, 0xff, 0xff, 0xff});
  Check(std::get<DecodedInstruction>(branch32).branch_delta == -4);

  const auto string_ref = Decode(std::array<std::uint8_t, 3>{0x3e, 0x34, 0x12});
  Check(std::get<DecodedInstruction>(string_ref).operands[0].kind ==
        OperandKind::kString);
  Check(std::get<DecodedInstruction>(string_ref).operands[0].raw_bits ==
        0x1234);

  const auto mixed_ids = Decode(std::array<std::uint8_t, 9>{
      0x35, 0x01, 0x34, 0x12, 0x78, 0x56, 0x00, 0x00, 0x02});
  Check(std::holds_alternative<DecodedInstruction>(mixed_ids));
  Check(std::get<DecodedInstruction>(mixed_ids).operands[1].kind ==
        OperandKind::kMethod);
  Check(std::get<DecodedInstruction>(mixed_ids).operands[1].raw_bits == 0x1234);
  Check(std::get<DecodedInstruction>(mixed_ids).operands[2].kind ==
        OperandKind::kLiteralArray);
  Check(std::get<DecodedInstruction>(mixed_ids).operands[2].raw_bits == 0x5678);

  const auto floating = Decode(std::array<std::uint8_t, 9>{
      0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x7f});
  Check(std::holds_alternative<DecodedInstruction>(floating));
  Check(std::get<DecodedInstruction>(floating).operands[0].raw_bits ==
        0x7ff8000000000000ULL);
  Check(std::get<DecodedInstruction>(floating).operands[0].is_float);

  Check(std::get<InstructionError>(Decode(std::array<std::uint8_t, 1>{0xfd})) ==
        InstructionError::kTruncated);
  Check(std::get<InstructionError>(Decode(std::array<std::uint8_t, 2>{
            0xfd, 0xff})) == InstructionError::kInvalidOpcode);
  Check(std::get<InstructionError>(Decode(std::array<std::uint8_t, 1>{0xff})) ==
        InstructionError::kInvalidOpcode);
  Check(std::get<InstructionError>(Decode(std::array<std::uint8_t, 1>{0x4e})) ==
        InstructionError::kTruncated);
  Check(std::get<InstructionError>(
            abcd::abc::DecodeInstruction(std::array<std::uint8_t, 1>{0x00},
                                         abcd::abc::ProfileId::kStatic0107)) ==
        InstructionError::kUnsupportedProfile);

  for (const EncodingCase encoding : kEncodingCases) {
    std::array<std::uint8_t, abcd::abc::kMaxInstructionBytes> bytes{};
    bytes[0] = static_cast<std::uint8_t>(encoding.opcode);
    if (encoding.opcode > UINT8_MAX) {
      bytes[1] = static_cast<std::uint8_t>(encoding.opcode >> 8);
    }
    const auto valid =
        abcd::abc::DecodeInstruction(std::span(bytes).first(encoding.size),
                                     abcd::abc::ProfileId::kDynamic1206);
    Check(std::holds_alternative<DecodedInstruction>(valid));
    Check(std::get<DecodedInstruction>(valid).size == encoding.size);
    for (std::size_t length = 0; length < encoding.size; ++length) {
      const auto truncated = abcd::abc::DecodeInstruction(
          std::span(bytes).first(length), abcd::abc::ProfileId::kDynamic1206);
      Check(std::get<InstructionError>(truncated) ==
            InstructionError::kTruncated);
    }
  }

  std::array<bool, UINT16_MAX + 1> valid_opcodes{};
  for (const EncodingCase encoding : kEncodingCases) {
    valid_opcodes[encoding.opcode] = true;
  }
  for (std::uint32_t primary = 0; primary <= UINT8_MAX; ++primary) {
    const bool prefix = primary >= 0xfb && primary <= 0xfe;
    if (!prefix && !valid_opcodes[primary]) {
      Check(std::get<InstructionError>(Decode(std::array<std::uint8_t, 1>{
                static_cast<std::uint8_t>(primary)})) ==
            InstructionError::kInvalidOpcode);
    }
    if (prefix) {
      for (std::uint32_t secondary = 0; secondary <= UINT8_MAX; ++secondary) {
        const std::uint16_t opcode = (secondary << 8) | primary;
        if (valid_opcodes[opcode]) continue;
        Check(std::get<InstructionError>(Decode(std::array<std::uint8_t, 2>{
                  static_cast<std::uint8_t>(primary),
                  static_cast<std::uint8_t>(secondary)})) ==
              InstructionError::kInvalidOpcode);
      }
    }
  }
}
