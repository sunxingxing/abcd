#include "abcd/analysis/class_heritage.h"

#include "analysis/ssa_fixture.h"

int main() {
  using namespace abcd;
  using analysis::HeritageKind;
  const auto check = [](std::vector<std::uint8_t> prefix,
                        HeritageKind expected) {
    prefix.insert(prefix.end(), {0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0x65});
    const auto body = test::Body(prefix);
    const auto result = analysis::ProveClassHeritage(body, 2);
    const auto& sites = std::get<std::vector<analysis::ClassHeritage>>(result);
    test::Check(sites.size() == 1 && sites[0].kind == expected);
  };
  check({0x70, 0x61, 0}, HeritageKind::kAbsent);
  check({0x01, 0x61, 0}, HeritageKind::kNull);
  check({0x00, 0x61, 0}, HeritageKind::kUnknown);
  check({}, HeritageKind::kUnknown);
  check({0x70, 0x61, 1, 0x60, 1, 0x61, 0}, HeritageKind::kAbsent);
  check({0x70, 0x61, 0, 0x00, 0x61, 0}, HeritageKind::kUnknown);
  // An unsupported operation kills old facts, but new definitions recover.
  check({0x70, 0x61, 0, 0x6f}, HeritageKind::kUnknown);
  check({0x6f, 0x70, 0x61, 0}, HeritageKind::kAbsent);
  // A supported call clobbers acc, not a caller-private parent register.
  check({0x70, 0x61, 0, 0x29, 0}, HeritageKind::kAbsent);
  check({0x70, 0x29, 0, 0x61, 0}, HeritageKind::kUnknown);
  const auto consecutive = test::Body(
      {0x70, 0x61, 0, 0x35, 0,    0, 0,    0, 0, 0, 0, 0, 0x35, 0, 0, 0,   0,
       0,    0,    0, 0,    0x61, 0, 0x35, 0, 0, 0, 0, 0, 0,    0, 0, 0x65});
  const auto consecutive_result =
      std::get<std::vector<analysis::ClassHeritage>>(
          analysis::ProveClassHeritage(consecutive, 2));
  test::Check(consecutive_result.size() == 3);
  test::Check(consecutive_result[0].kind == HeritageKind::kAbsent &&
              consecutive_result[1].kind == HeritageKind::kAbsent &&
              consecutive_result[2].kind == HeritageKind::kUnknown);
  // The forward edge bypasses the definition; no fallthrough fact may leak.
  const auto joined =
      test::Body({0x4f, 5, 0x70, 0x61, 0, 0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0x65});
  test::Check(std::get<std::vector<analysis::ClassHeritage>>(
                  analysis::ProveClassHeritage(joined, 2))[0]
                  .kind == HeritageKind::kUnknown);
  auto branch =
      test::Body({0x70, 0x61, 0, 0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0x4d, 0xf7});
  test::Check(std::get<std::vector<analysis::ClassHeritage>>(
                  analysis::ProveClassHeritage(branch, 2))[0]
                  .kind == HeritageKind::kUnknown);
  const auto body =
      test::Body({0x70, 0x61, 0, 0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0x65});
  test::Check(std::get<analysis::HeritageError>(analysis::ProveClassHeritage(
                  body, 0)) == analysis::HeritageError::kFrameRange);
  test::Check(std::get<analysis::HeritageError>(analysis::ProveClassHeritage(
                  body, 2, {0})) == analysis::HeritageError::kResourceLimit);
  auto handled = body;
  handled.handlers.push_back({{}, {3}, 1});
  test::Check(std::get<std::vector<analysis::ClassHeritage>>(
                  analysis::ProveClassHeritage(handled, 2))[0]
                  .kind == HeritageKind::kUnknown);
  handled.handlers[0].offset = {0};
  test::Check(std::get<std::vector<analysis::ClassHeritage>>(
                  analysis::ProveClassHeritage(handled, 2))[0]
                  .kind == HeritageKind::kAbsent);
  auto malformed = body;
  malformed.instructions[0].decoded.operand_count = 1;
  test::Check(std::get<analysis::HeritageError>(
                  analysis::ProveClassHeritage(malformed, 2)) ==
              analysis::HeritageError::kMalformedInstruction);
  for (const std::size_t count : {200, 201, 400, 800, 1600}) {
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i < count; ++i)
      bytes.insert(bytes.end(),
                   {0x6f, 0x70, 0x61, 0, 0x35, 0, 0, 0, 0, 0, 0, 0, 0});
    bytes.push_back(0x65);
    const auto many = test::Body(bytes);
    const auto result = analysis::ProveClassHeritage(many, count);
    const auto& sites = std::get<std::vector<analysis::ClassHeritage>>(result);
    test::Check(sites.size() == count);
    for (const auto& site : sites)
      test::Check(site.kind == HeritageKind::kAbsent);
  }
  std::size_t low = 0, high = analysis::HeritageBudget{}.bytes;
  const auto fits = [&](std::size_t bytes) {
    return std::holds_alternative<std::vector<analysis::ClassHeritage>>(
        analysis::ProveClassHeritage(handled, 2, {bytes}));
  };
  test::Check(!fits(low) && fits(high));
  while (high - low > 1) {
    const auto middle = low + (high - low) / 2;
    if (fits(middle))
      high = middle;
    else
      low = middle;
  }
  test::Check(fits(high) && !fits(high - 1));
}
