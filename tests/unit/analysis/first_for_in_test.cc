#include "abcd/analysis/first_for_in.h"

#include "analysis/ssa_fixture.h"

namespace {
abcd::abc::MethodBody FirstKey() {
  auto body = abcd::test::Body({
      0xd5, 0x60, 0x05, 0x66, 0x61, 0x01, 0x4d, 0x08,
      0x61, 0x00, 0x60, 0x00, 0xfe, 0x00, 0x36, 0x01,
      0x61, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x51, 0x05,
      0x60, 0x00, 0x64, 0x65});
  body.tries.push_back({{1}, 5, 0, 1});
  body.handlers.push_back({{}, {8}, 0});
  return body;
}
}  // namespace

int main() {
  using namespace abcd;
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(2, 4, {true, true, true, true}));
  const auto body = FirstKey();
  const auto match = analysis::MatchFirstForIn(body, frame);
  test::Check(match && match->source.value == 5);
  auto altered = body;
  altered.tries[0].length = 7;
  test::Check(!analysis::MatchFirstForIn(altered, frame));
  altered = body;
  altered.handlers[0].type = abc::CatchTypeIndex{0};
  test::Check(!analysis::MatchFirstForIn(altered, frame));
  altered = body;
  altered.instructions[4].branch_target = abc::CodeOffset{8};
  test::Check(!analysis::MatchFirstForIn(altered, frame));
  altered = body;
  altered.instructions[4].decoded.operands[0].raw_bits = 7;
  test::Check(!analysis::MatchFirstForIn(altered, frame));
  altered = body;
  altered.instructions[8].decoded.operands[0].raw_bits = 0;
  test::Check(!analysis::MatchFirstForIn(altered, frame));
  altered = body;
  altered.instructions[1].decoded.operands[0].raw_bits = 4;
  test::Check(!analysis::MatchFirstForIn(altered, frame));
  altered = body;
  altered.instructions.push_back(body.instructions.back());
  test::Check(!analysis::MatchFirstForIn(altered, frame));
  altered = body;
  altered.instructions.front().offset = abc::CodeOffset{1};
  test::Check(!analysis::MatchFirstForIn(altered, frame));
}
