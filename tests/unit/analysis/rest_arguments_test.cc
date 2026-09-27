#include "abcd/analysis/rest_arguments.h"

#include <cstdint>

#include "abcd/analysis/register_effect.h"
#include "analysis/ssa_fixture.h"

int main() {
  using namespace abcd;
  const auto narrow = test::Body({0xcf, 0x00}).instructions.front().decoded;
  test::Check(std::get<analysis::RestCopy>(
                  analysis::LiftRestArguments(narrow)).first_argument == 0);
  const auto wide = test::Body({0xfd, 0x0b, 0x34, 0x12})
                        .instructions.front().decoded;
  test::Check(std::get<analysis::RestCopy>(
                  analysis::LiftRestArguments(wide)).first_argument == 0x1234);
  const auto effect = std::get<analysis::RegisterEffect>(
      analysis::LiftRegisterEffect(narrow, 8));
  test::Check(effect.read_count == 0 && effect.write &&
              std::holds_alternative<analysis::Accumulator>(*effect.write));
  auto malformed = narrow;
  malformed.operand_count = 0;
  test::Check(std::get<analysis::RestError>(
                  analysis::LiftRestArguments(malformed)) ==
              analysis::RestError::kMalformedInstruction);
  malformed = wide;
  malformed.operands[0].raw_bits = UINT32_MAX;
  test::Check(std::get<analysis::RestError>(
                  analysis::LiftRestArguments(malformed)) ==
              analysis::RestError::kMalformedInstruction);

  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      4, 4, {true, true, true, true}));
  auto body = test::Body({0x44, 0x40, 0x44, 0x51, 0x44, 0x62,
                          0x44, 0x73, 0xcf, 0, 0x61, 3, 0x60, 3, 0x64});
  test::Check(analysis::ProveRestPrologue(body, frame, 0).has_value());
  const auto snapshot = test::Body({0x44, 0x40, 0x44, 0x51, 0x44, 0x62,
      0x44, 0x73, 0xcf, 0, 0x61, 3,
      static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  // The runtime snapshot may read the original rest argument slot even though
  // its encoding has no explicit register operand. It is not a rest-array read.
  test::Check(!analysis::ProveRestPrologue(snapshot, frame, 0));
  body.tries.push_back({{0}, 1, 0, 0});
  test::Check(!analysis::ProveRestPrologue(body, frame, 0));
  body.tries.clear();
  body.instructions.back().branch_target = abc::CodeOffset{0};
  test::Check(!analysis::ProveRestPrologue(body, frame, 0));
  body.instructions.back().branch_target.reset();
  const auto repeated = test::Body({0x44, 0x40, 0x44, 0x51,
                                    0x44, 0x62, 0x44, 0x73,
                                    0xcf, 0, 0x61, 3,
                                    0xcf, 0, 0x60, 3, 0x64});
  test::Check(!analysis::ProveRestPrologue(repeated, frame, 0));
  for (std::size_t count : {std::size_t{200}, std::size_t{201}}) {
    auto scaled = body;
    const auto nop = test::Body({0x00}).instructions.front();
    scaled.instructions.insert(scaled.instructions.end(), count, nop);
    test::Check(analysis::ProveRestPrologue(scaled, frame, 0).has_value());
  }
}
