#include "abcd/analysis/isa_catalog.h"

#include "analysis/ssa_fixture.h"

int main() {
  using namespace abcd;
  using G = analysis::IsaGroup;
  // Counts are an independent snapshot of the pinned v7 YAML, in group order.
  constexpr std::array<std::size_t, static_cast<std::size_t>(G::kCount)> expected{
      18, 12, 26, 18, 16, 4, 26, 12, 31, 8, 113, 1, 37, 3, 1, 1, 2, 2, 1};
  std::array<std::size_t, expected.size()> counts{};
  std::optional<abc::Opcode> previous;
  constexpr unsigned kByteBits = 8;
  for (const auto& entry : analysis::PinnedIsaCatalog()) {
    test::Check(!previous || *previous < entry.opcode);
    previous = entry.opcode;
    test::Check(analysis::FindPinnedInstruction(entry.opcode) == &entry);
    const auto group = static_cast<std::size_t>(entry.group);
    test::Check(group < counts.size());
    ++counts[group];
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
  test::Check(counts == expected);
  test::Check(analysis::FindPinnedInstruction(abc::Opcode::kLDUNDEFINED)->group == G::kConstant);
  test::Check(analysis::FindPinnedInstruction(abc::Opcode::kNOP)->group == G::kNop);
  test::Check(!analysis::FindPinnedInstruction(static_cast<abc::Opcode>(UINT16_MAX)));
}
