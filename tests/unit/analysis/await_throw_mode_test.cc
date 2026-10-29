#include "abcd/analysis/await_region.h"
#include "abcd/analysis/coroutine.h"
#include "analysis/ssa_fixture.h"

#include <vector>

using namespace abcd;
using namespace abcd::analysis;
namespace {
constexpr std::uint64_t kFrame = 8;
constexpr std::size_t kBytes = 1 << 20;
std::uint8_t Op(abc::Opcode value) { return static_cast<std::uint8_t>(value); }
// The await protocol with its THROW constant read by `load`, followed by
// `continuation`. v0 is the generator, v1/v2 the result/mode, v5 a constant.
std::vector<std::uint8_t> Protocol(std::vector<std::uint8_t> prelude,
                                   std::vector<std::uint8_t> load,
                                   std::vector<std::uint8_t> continuation) {
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  auto bytes = prelude;
  bytes.insert(bytes.end(), {
      Op(abc::Opcode::kLDA_V8), 3, Op(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8), 0,
      Op(abc::Opcode::kSUSPENDGENERATOR_V8), 0, Op(abc::Opcode::kLDA_V8), 0,
      Op(abc::Opcode::kRESUMEGENERATOR), Op(abc::Opcode::kSTA_V8), 1,
      Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kGETRESUMEMODE), Op(abc::Opcode::kSTA_V8), 2});
  bytes.insert(bytes.end(), load.begin(), load.end());
  bytes.insert(bytes.end(), {
      Op(abc::Opcode::kEQ_IMM8_V8), 0, 2, Op(abc::Opcode::kJEQZ_IMM8), 6,
      Op(abc::Opcode::kLDA_V8), 1,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> 8)});
  bytes.insert(bytes.end(), continuation.begin(), continuation.end());
  bytes.insert(bytes.end(), {Op(abc::Opcode::kASYNCFUNCTIONRESOLVE_V8), 0, Op(abc::Opcode::kRETURN)});
  return bytes;
}
std::vector<std::uint8_t> Literal(std::uint8_t value) {
  return {Op(abc::Opcode::kLDAI_IMM32), value, 0, 0, 0};
}
std::vector<std::uint8_t> Store(std::vector<std::uint8_t> bytes, std::uint8_t slot) {
  bytes.insert(bytes.end(), {Op(abc::Opcode::kSTA_V8), slot});
  return bytes;
}
bool Accepted(const std::vector<std::uint8_t>& bytes) {
  return std::holds_alternative<std::vector<AwaitRegion>>(
      FindAwaitRegions(test::Body(bytes), abc::ProfileId::kDynamic1206, kFrame, kBytes));
}
bool Shape(const std::vector<std::uint8_t>& bytes) {
  const auto found = FindAwaitRegions(test::Body(bytes), abc::ProfileId::kDynamic1206, kFrame, kBytes);
  const auto* failure = std::get_if<AwaitRegionFailure>(&found);
  return failure && failure->kind == AwaitRegionError::kUnsupportedShape;
}
}  // namespace

int main() {
  const auto one = static_cast<std::uint8_t>(CoroutineResumeMode::kThrow);
  const std::vector<std::uint8_t> reload{Op(abc::Opcode::kLDA_V8), 1};
  const std::vector<std::uint8_t> from_v5{Op(abc::Opcode::kLDA_V8), 5};
  // The pinned literal form and a register proved to hold the same integer.
  test::Check(Accepted(Protocol({}, Literal(one), reload)));
  test::Check(Accepted(Protocol(Store(Literal(one), 5), from_v5, reload)));
  // Another constant or an unknown value is not the THROW mode.
  test::Check(Shape(Protocol(Store(Literal(2), 5), from_v5, reload)));
  test::Check(Shape(Protocol({}, from_v5, reload)));
  // One path overwrites v5 before the await: every path must hold the integer.
  auto split = Store(Literal(one), 5);
  split.insert(split.end(), {Op(abc::Opcode::kLDTRUE), Op(abc::Opcode::kJEQZ_IMM8), 9});
  const auto overwrite = Store(Literal(0), 5);
  split.insert(split.end(), overwrite.begin(), overwrite.end());
  test::Check(Shape(Protocol(split, from_v5, reload)));
  // The continuation may overwrite the accumulator, never read the comparison.
  test::Check(Accepted(Protocol({}, Literal(one), Literal(0))));
  test::Check(Shape(Protocol({}, Literal(one), {Op(abc::Opcode::kSTA_V8), 3})));
  return 0;
}
