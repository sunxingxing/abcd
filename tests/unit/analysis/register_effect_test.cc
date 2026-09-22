#include "abcd/analysis/register_effect.h"

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <vector>

namespace {
void Check(bool value,
           std::source_location where = std::source_location::current()) {
  if (!value) {
    std::cerr << where.line() << '\n';
    std::abort();
  }
}
bool Same(abcd::analysis::ScalarSlot a, abcd::analysis::ScalarSlot b) {
  const auto* left = std::get_if<abcd::abc::RegisterId>(&a);
  const auto* right = std::get_if<abcd::abc::RegisterId>(&b);
  return left && right ? left->value == right->value : !left && !right;
}
abcd::abc::DecodedInstruction Decode(
    std::initializer_list<std::uint8_t> bytes) {
  return std::get<abcd::abc::DecodedInstruction>(
      abcd::abc::DecodeInstruction(std::span(bytes.begin(), bytes.size()),
                                   abcd::abc::ProfileId::kDynamic1206));
}
}  // namespace
int main() {
  using namespace abcd;
  using namespace analysis;
  const auto store =
      std::get<RegisterEffect>(LiftRegisterEffect(Decode({0x3d, 0}), 3));
  Check(store.read_count == 1 &&
        std::holds_alternative<Accumulator>(store.reads[0]));
  Check(!store.write);
  const auto legacy = std::get<RegisterEffect>(
      LiftRegisterEffect(Decode({0xfc, 0x20, 0, 2}), 3));
  Check(legacy.read_count == 1 &&
        std::get<abc::RegisterId>(legacy.reads[0]).value == 2);
  Check(!legacy.write);
  const ScalarSlot acc = Accumulator{}, reg = abc::RegisterId{2};
  struct Case {
    std::initializer_list<std::uint8_t> code;
    std::vector<ScalarSlot> reads;
    std::optional<ScalarSlot> write;
  };
  const Case cases[]{{{0x60, 2}, {reg}, acc},
                     {{0x61, 2}, {acc}, reg},
                     {{0x45, 2, 2}, {reg}, reg},
                     {{0x0a, 0, 2}, {reg, acc}, acc},
                     {{0x1f, 0}, {acc}, acc},
                     {{0x37, 0, 2}, {reg, acc}, acc},
                     {{0}, {}, acc},
                     {{0x62, 1, 0, 0, 0}, {}, acc},
                     {{0x3e, 0, 0}, {}, acc},
                     {{0x42, 0, 0, 0}, {acc}, acc},
                     {{0x43, 0, 0, 0, 2}, {reg, acc}, {}},
                     {{0x3a, 0, 0, 0}, {acc}, acc},
                     {{0xfc, 29, 2, 0, 0, 0, 0}, {reg}, acc},
                     {{0x29, 0}, {acc}, acc},
                     {{0x2c, 0, 2, 2, 2}, {acc, reg, reg, reg}, acc},
                     {{0x3c, 0}, {}, acc},
                     {{0x09, 2}, {}, acc},
                     {{0x69}, {}, {}},
                     {{0xfc, 0}, {}, acc},
                     {{0x70}, {}, acc},
                     {{0xfe, 6, 2, 3}, {reg, abc::RegisterId{3}}, {}},
                     {{0xfe, 9, 0, 0}, {acc}, {}},
                     {{0x33, 0, 0, 0, 0}, {}, acc},
                     {{0x34, 0, 0, 0, 0}, {acc}, acc},
                     {{0x4f, 0}, {acc}, {}},
                     {{0x4d, 0}, {}, {}},
                     {{0x64}, {acc}, {}},
                     {{0x65}, {}, {}},
                     {{0xfe, 0}, {acc}, {}},
                     {{0xd5}, {}, {}}};
  for (const auto& item : cases) {
    auto decoded = Decode(item.code);
    for (const bool flip_flags : {false, true}) {
      if (flip_flags) {
        decoded.accumulator_read = !decoded.accumulator_read;
        decoded.accumulator_write = !decoded.accumulator_write;
        for (auto& operand : decoded.operands)
          operand.access = abc::OperandAccess::kReadWrite;
      }
      const auto effect =
          std::get<RegisterEffect>(LiftRegisterEffect(decoded, 4));
      Check(effect.read_count == item.reads.size() && !effect.range);
      for (std::size_t i = 0; i < item.reads.size(); ++i)
        Check(Same(effect.reads[i], item.reads[i]));
      Check(effect.write.has_value() == item.write.has_value());
      if (item.write) Check(Same(*effect.write, *item.write));
    }
    decoded.operand_count = UINT8_MAX;
    Check(std::get<ScalarError>(LiftRegisterEffect(decoded, 4)) ==
          ScalarError::kMalformedInstruction);
  }
  const auto range = std::get<RegisterEffect>(
      LiftRegisterEffect(Decode({0x73, 0, 255, 255}), 510));
  Check(range.read_count == 1 && Same(range.reads[0], acc));
  Check(range.range->first.value == 255 && range.range->count == 255);
  Check(Same(*range.write, acc));
  Check(
      std::get<RegisterEffect>(LiftRegisterEffect(Decode({0x73, 0, 0, 255}), 0))
          .range->count == 0);
  for (const auto code : {std::initializer_list<std::uint8_t>{0x60, 2},
                          {0xfc, 0x20, 0, 2},
                          {0xfe, 6, 2, 2},
                          {0x43, 0, 0, 0, 2},
                          {0xfc, 29, 2, 0, 0, 0, 0},
                          {0x2a, 0, 2}})
    Check(std::get<ScalarError>(LiftRegisterEffect(Decode(code), 2)) ==
          ScalarError::kRegisterRange);
  for (const auto code : {std::initializer_list<std::uint8_t>{0xfd, 4, 1, 0, 1},
                          {0xfc, 7, 0},
                          {0x26, 0, 0},
                          {0xfc, 27, 0, 1}})
    Check(std::get<ScalarError>(LiftRegisterEffect(Decode(code), 2)) ==
          ScalarError::kUnsupportedOpcode);
}
