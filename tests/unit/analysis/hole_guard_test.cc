#include "abcd/analysis/hole_guard.h"

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
  Check(std::holds_alternative<analysis::LoadHole>(
      analysis::LiftHoleInstruction(Decode({0x70}), 0)));
  const auto registers = Decode({0xfe, 6, 2, 3});
  const auto guard = std::get<analysis::HoleGuard>(
      analysis::LiftHoleInstruction(registers, 4));
  Check(std::get<abc::RegisterId>(guard.value).value == 2);
  Check(std::get<abc::RegisterId>(guard.name).value == 3);
  Check(std::get<analysis::HoleError>(analysis::LiftHoleInstruction(
            registers, 3)) == analysis::HoleError::kRegisterRange);
  const auto named = Decode({0xfe, 9, 0xff, 0xff});
  const auto named_guard =
      std::get<analysis::HoleGuard>(analysis::LiftHoleInstruction(named, 0));
  Check(std::holds_alternative<analysis::Accumulator>(named_guard.value));
  Check(std::get<analysis::HoleNameSlot>(named_guard.name).slot.value == 65535);
  for (auto instruction : {registers, named, Decode({0x70})}) {
    instruction.operand_count = 7;
    Check(std::get<analysis::HoleError>(analysis::LiftHoleInstruction(
              instruction, 4)) == analysis::HoleError::kMalformedInstruction);
  }
  auto malformed = named;
  malformed.operands[0].raw_bits = 65536;
  Check(std::get<analysis::HoleError>(analysis::LiftHoleInstruction(
            malformed, 4)) == analysis::HoleError::kMalformedInstruction);
  malformed = registers;
  malformed.operands[1].kind = abc::OperandKind::kImmediate;
  Check(std::get<analysis::HoleError>(analysis::LiftHoleInstruction(
            malformed, 4)) == analysis::HoleError::kMalformedInstruction);
  for (const auto index : {0U, 1U}) {
    malformed = registers;
    malformed.operands[index].raw_bits = 256;
    Check(std::get<analysis::HoleError>(analysis::LiftHoleInstruction(
              malformed, 257)) == analysis::HoleError::kMalformedInstruction);
  }
  const auto last_register = Decode({0xfe, 6, 0xff, 0xff});
  Check(std::holds_alternative<analysis::HoleGuard>(
      analysis::LiftHoleInstruction(last_register, 256)));
  Check(std::get<analysis::HoleError>(analysis::LiftHoleInstruction(
            last_register, 255)) == analysis::HoleError::kRegisterRange);
  malformed = named;
  malformed.operands[0].kind = abc::OperandKind::kImmediate;
  Check(std::get<analysis::HoleError>(analysis::LiftHoleInstruction(
            malformed, 0)) == analysis::HoleError::kMalformedInstruction);
  Check(std::get<analysis::HoleError>(analysis::LiftHoleInstruction(
            Decode({0}), 0)) == analysis::HoleError::kUnsupportedOpcode);
}
