#include "abcd/abc/register_access.h"

#include <cstdlib>
#include <initializer_list>
#include <vector>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
abcd::abc::DecodedInstruction Decode(
    std::initializer_list<std::uint8_t> bytes) {
  const std::vector<std::uint8_t> storage(bytes);
  return std::get<abcd::abc::DecodedInstruction>(abcd::abc::DecodeInstruction(
      storage, abcd::abc::ProfileId::kDynamic1206));
}
}  // namespace

int main() {
  using namespace abcd::abc;
  struct RangeCase {
    std::initializer_list<std::uint8_t> bytes;
    std::uint32_t expected;
  };
  const RangeCase ranges[]{
      {{0x73, 0, 2, 10}, 2},       {{0xfd, 4, 2, 0, 10}, 2},
      {{0x32, 0, 2, 10}, 2},       {{0xfd, 6, 2, 0, 10}, 2},
      {{0xbb, 0, 2, 10}, 2},       {{0xfd, 7, 2, 0, 10}, 2},
      {{0x31, 0, 2, 10}, 3},       {{0xfd, 5, 2, 0, 10}, 3},
      {{0xe1, 0, 2, 0, 0, 10}, 3}, {{0xfd, 0x14, 2, 0, 0, 0, 10}, 3},
      {{0xfc, 0x0f, 2, 0, 10}, 3}, {{0xfc, 0x11, 2, 0, 10}, 3},
      {{0x08, 0, 2, 10}, 2},       {{0x83, 0, 0, 2, 10}, 2},
      {{0xfd, 1, 2, 0, 10}, 2},    {{0xb3, 2, 3, 10}, 3},
      {{0xfd, 0, 2, 0, 3, 10}, 3}};
  for (const auto& test : ranges) {
    const auto decoded = Decode(test.bytes);
    const auto result = std::get<RegisterAccesses>(
        GetRegisterAccesses(decoded, 10 + test.expected));
    Check(result.windows[result.count - 1].first.value == 10 &&
          result.windows[result.count - 1].count == test.expected);
    Check(std::holds_alternative<RegisterAccessError>(
        GetRegisterAccesses(decoded, 9 + test.expected)));
  }
  const auto move =
      std::get<RegisterAccesses>(GetRegisterAccesses(Decode({0x44, 0xa3}), 11));
  Check(move.count == 2 && move.windows[0].first.value == 3 &&
        move.windows[0].access == OperandAccess::kWrite &&
        move.windows[1].first.value == 10 &&
        move.windows[1].access == OperandAccess::kRead);
  // callrange's encoded base is not read when the argument count is zero.
  auto instruction = Decode({0x73, 0, 0, 10});
  auto accesses =
      std::get<RegisterAccesses>(GetRegisterAccesses(instruction, 0));
  Check(accesses.count == 0 && accesses.accumulator_read &&
        accesses.accumulator_write);
  instruction = Decode({0x31, 0, 2, 10});  // receiver plus two arguments
  accesses = std::get<RegisterAccesses>(GetRegisterAccesses(instruction, 13));
  Check(accesses.count == 1 && accesses.windows[0].first.value == 10 &&
        accesses.windows[0].count == 3);
  Check(std::get<RegisterAccessError>(GetRegisterAccesses(instruction, 12)) ==
        RegisterAccessError::kFrameRange);
  instruction = Decode({0xfc, 0x0f, 2, 0, 10});  // old callee plus arguments
  accesses = std::get<RegisterAccesses>(GetRegisterAccesses(instruction, 13));
  Check(accesses.windows[0].count == 3 && !accesses.accumulator_read);
  instruction = Decode({0x08, 0, 0, 10});
  Check(std::get<RegisterAccessError>(GetRegisterAccesses(instruction, 11)) ==
        RegisterAccessError::kInvalidCount);
  instruction = Decode({0xb3, 2, 3, 10});  // object and three excluded keys
  accesses = std::get<RegisterAccesses>(GetRegisterAccesses(instruction, 13));
  Check(accesses.count == 2 && accesses.windows[0].first.value == 3 &&
        accesses.windows[0].count == 1 && accesses.windows[1].count == 3);
  for (const std::uint32_t count : {200U, 201U, 1024U, 65535U}) {
    instruction = Decode({0xfd, 0x05, 0, 0, 1});
    instruction.operands[0].raw_bits = count;
    accesses =
        std::get<RegisterAccesses>(GetRegisterAccesses(instruction, count + 2));
    Check(accesses.count == 1 && accesses.windows[0].count == count + 1);
  }
}
