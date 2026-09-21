#include "abcd/analysis/lexical.h"

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
  using namespace abcd::analysis;
  for (auto bytes : {std::initializer_list<std::uint8_t>{0x3c, 0x43},
                     {0x8a, 3, 4},
                     {0xfd, 0x0c, 3, 0, 4, 0}}) {
    const auto load =
        std::get<LexicalLoad>(LiftLexicalInstruction(Decode(bytes), 0));
    Check(load.address.depth.value == 3 && load.address.slot.value == 4);
  }
  for (auto bytes : {std::initializer_list<std::uint8_t>{0x3d, 0x43},
                     {0x8b, 3, 4},
                     {0xfd, 0x0d, 3, 0, 4, 0}}) {
    const auto store =
        std::get<LexicalStore>(LiftLexicalInstruction(Decode(bytes), 0));
    Check(store.address.depth.value == 3 && store.address.slot.value == 4);
    Check(std::holds_alternative<Accumulator>(store.value));
  }
  for (auto bytes : {std::initializer_list<std::uint8_t>{0xfc, 0x20, 0x43, 2},
                     {0xfc, 0x21, 3, 4, 2},
                     {0xfc, 0x22, 3, 0, 4, 0, 2}}) {
    const auto decoded = Decode(bytes);
    const auto store =
        std::get<LexicalStore>(LiftLexicalInstruction(decoded, 3));
    Check(store.address.depth.value == 3 && store.address.slot.value == 4);
    Check(std::get<abcd::abc::RegisterId>(store.value).value == 2);
    Check(std::get<LexicalError>(LiftLexicalInstruction(decoded, 2)) ==
          LexicalError::kRegisterRange);
  }
  for (auto bytes : {std::initializer_list<std::uint8_t>{0x09, 201},
                     {0xfd, 2, 201, 0},
                     {0xb6, 201, 0x34, 0x12},
                     {0xfd, 3, 201, 0, 0x34, 0x12}}) {
    const auto decoded = Decode(bytes);
    const auto push =
        std::get<PushEnvironment>(LiftLexicalInstruction(decoded, 0));
    Check(push.slots == 201);
    Check(push.scope.has_value() == (decoded.operand_count == 2));
    if (push.scope) Check(push.scope->value == 0x1234);
    auto malformed = decoded;
    malformed.operands[0].raw_bits = UINT64_MAX;
    Check(std::get<LexicalError>(LiftLexicalInstruction(malformed, 0)) ==
          LexicalError::kMalformedInstruction);
  }
  Check(std::holds_alternative<PopEnvironment>(
      LiftLexicalInstruction(Decode({0x69}), 0)));
  Check(std::holds_alternative<PopEnvironment>(
      LiftLexicalInstruction(Decode({0xfc, 1}), 0)));
  Check(std::holds_alternative<CurrentEnvironment>(
      LiftLexicalInstruction(Decode({0xfc, 0}), 0)));
  Check(std::get<LexicalError>(LiftLexicalInstruction(Decode({0}), 0)) ==
        LexicalError::kUnsupportedOpcode);
  const auto wide = std::get<LexicalLoad>(
      LiftLexicalInstruction(Decode({0xfd, 0x0c, 0xff, 0xff, 0xff, 0xff}), 0));
  Check(wide.address.depth.value == 65535 && wide.address.slot.value == 65535);
  for (const std::uint16_t count : {0, 200, 201, 65535}) {
    const auto push = std::get<PushEnvironment>(LiftLexicalInstruction(
        Decode({0xfd, 2, static_cast<std::uint8_t>(count),
                static_cast<std::uint8_t>(count >> 8)}),
        0));
    Check(push.slots == count && !push.scope);
  }
  auto malformed = Decode({0x3c, 0});
  malformed.operands[0].raw_bits = 16;
  Check(std::get<LexicalError>(LiftLexicalInstruction(malformed, 0)) ==
        LexicalError::kMalformedInstruction);
  malformed = Decode({0xb6, 0, 0, 0});
  malformed.operands[1].kind = abcd::abc::OperandKind::kString;
  Check(std::get<LexicalError>(LiftLexicalInstruction(malformed, 0)) ==
        LexicalError::kMalformedInstruction);
  malformed = Decode({0x69});
  malformed.operand_count = 1;
  Check(std::get<LexicalError>(LiftLexicalInstruction(malformed, 0)) ==
        LexicalError::kMalformedInstruction);
}
