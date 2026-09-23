#include "abcd/analysis/private_property.h"

#include <array>
#include <cstdlib>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
template <std::size_t N>
abcd::abc::DecodedInstruction Decode(const std::array<std::uint8_t, N>& bytes) {
  return std::get<abcd::abc::DecodedInstruction>(
      abcd::abc::DecodeInstruction(bytes, abcd::abc::ProfileId::kDynamic1301));
}
}  // namespace

int main() {
  using namespace abcd;
  using namespace analysis;
  const auto read = Decode(
      std::array<std::uint8_t, 6>{0xd8, 4, 0x34, 0x12, 0x78, 0x56});
  const auto loaded = std::get<PrivateLoad>(LiftPrivateProperty(read, 2));
  Check(loaded.name.inline_cache == 4 && loaded.name.level == 0x1234 &&
        loaded.name.slot == 0x5678);
  const auto store = Decode(
      std::array<std::uint8_t, 7>{0xd9, 4, 0x34, 0x12, 0x78, 0x56, 1});
  const auto stored = std::get<PrivateStore>(LiftPrivateProperty(store, 2));
  Check(stored.name.level == loaded.name.level && stored.object.value == 1);
  Check(std::get<PrivatePropertyError>(LiftPrivateProperty(store, 1)) ==
        PrivatePropertyError::kRegisterRange);
  const auto test = Decode(
      std::array<std::uint8_t, 6>{0xda, 4, 0x34, 0x12, 0x78, 0x56});
  Check(std::holds_alternative<PrivateHas>(LiftPrivateProperty(test, 2)));
  auto malformed = store;
  malformed.operands[3].kind = abc::OperandKind::kImmediate;
  Check(std::get<PrivatePropertyError>(LiftPrivateProperty(malformed, 2)) ==
        PrivatePropertyError::kMalformedInstruction);
  malformed = read;
  malformed.operands[1].raw_bits = 0x10000;
  Check(std::get<PrivatePropertyError>(LiftPrivateProperty(malformed, 2)) ==
        PrivatePropertyError::kMalformedInstruction);
  malformed = test;
  malformed.operand_count = 2;
  Check(std::get<PrivatePropertyError>(LiftPrivateProperty(malformed, 2)) ==
        PrivatePropertyError::kMalformedInstruction);
  const auto unrelated = Decode(std::array<std::uint8_t, 1>{0x64});
  Check(std::get<PrivatePropertyError>(LiftPrivateProperty(unrelated, 2)) ==
        PrivatePropertyError::kUnsupportedOpcode);
}
