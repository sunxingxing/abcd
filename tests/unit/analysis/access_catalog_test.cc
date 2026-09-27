#include "abcd/analysis/access_catalog.h"
#include "abcd/abc/instruction.h"

#include "analysis/ssa_fixture.h"

int main() {
  using namespace abcd;
  const auto entries = analysis::PinnedAccessCatalog();
  test::Check(!entries.empty());
  std::optional<abc::Opcode> previous;
  constexpr unsigned kByteBits = 8;
  for (const auto& entry : entries) {
    test::Check(!previous || *previous < entry.opcode);
    previous = entry.opcode;
    test::Check(analysis::FindPinnedAccess(entry.opcode) == &entry);
    std::array<std::uint8_t, abc::kMaxInstructionBytes> bytes{};
    const auto opcode = static_cast<std::uint16_t>(entry.opcode);
    bytes[0] = static_cast<std::uint8_t>(opcode);
    if (opcode > UINT8_MAX) bytes[1] = static_cast<std::uint8_t>(opcode >> kByteBits);
    const auto decoded = abc::DecodeInstruction(std::span(bytes).first(entry.size),
        abc::ProfileId::kDynamic1206);
    const auto& instruction = std::get<abc::DecodedInstruction>(decoded);
    test::Check(instruction.opcode == entry.opcode && instruction.size == entry.size);
    test::Check(instruction.accumulator_read == entry.reads_acc &&
        instruction.accumulator_write == entry.writes_acc);
    for (std::size_t length = 0; length < entry.size; ++length)
      test::Check(std::get<abc::InstructionError>(abc::DecodeInstruction(
          std::span(bytes).first(length), abc::ProfileId::kDynamic1206)) ==
          abc::InstructionError::kTruncated);
  }
  using F = analysis::AccessFamily;
  using O = abc::Opcode;
  test::Check(analysis::FindPinnedAccess(O::kSTOBJBYVALUE_IMM8_V8_V8)->family == F::kProperty);
  test::Check(analysis::FindPinnedAccess(O::kWIDE_LDOBJBYINDEX_PREF_IMM32)->family == F::kProperty);
  test::Check(analysis::FindPinnedAccess(O::kSTOWNBYNAME_IMM8_ID16_V8)->family == F::kOwnDefinition);
  test::Check(analysis::FindPinnedAccess(O::kLDLEXVAR_IMM4_IMM4)->family == F::kLexical);
  test::Check(analysis::FindPinnedAccess(O::kGETASYNCITERATOR_IMM8)->family == F::kIterator);
  test::Check(analysis::FindPinnedAccess(O::kGETRESUMEMODE)->family == F::kCoroutine);
  test::Check(!analysis::FindPinnedAccess(O::kSTRICTEQ_IMM8_V8));
  test::Check(!analysis::FindPinnedAccess(static_cast<O>(UINT16_MAX)));
}
