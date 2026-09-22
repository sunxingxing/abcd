#include "abcd/analysis/environment_escape.h"

#include "analysis/ssa_fixture.h"

using namespace abcd;
using namespace analysis;
using namespace abcd::test;
namespace abcd::test {
void VerifyEscapeOracle();
void VerifyEscapeScaling();
}  // namespace abcd::test
namespace {
auto Analyze(const abc::MethodBody& body) {
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  IdfQuota quota{UINT64_MAX};
  const auto ssa =
      std::get<RegisterSsa>(RegisterSsa::Build(body, graph, 10, quota));
  const auto flow =
      std::get<EnvironmentFlow>(AnalyzeEnvironments(body, graph, 10));
  return EnvironmentEscape::Build(body, ssa, flow, 10, quota);
}
}  // namespace
int main() {
  // A local cell contains its own environment; load it and return the alias.
  const auto body = Body({0x09, 1, 0x3d, 0, 0x3c, 0, 0x64});
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  IdfQuota quota{UINT64_MAX};
  const auto ssa =
      std::get<RegisterSsa>(RegisterSsa::Build(body, graph, 1, quota));
  const auto flow =
      std::get<EnvironmentFlow>(AnalyzeEnvironments(body, graph, 1));
  const auto result = EnvironmentEscape::Build(body, ssa, flow, 1, quota);
  Check(std::holds_alternative<EnvironmentEscape>(result));
  Check(std::get<EnvironmentEscape>(result).MayEscape({0}) == true);
  struct Case {
    std::initializer_list<std::uint8_t> code;
    std::initializer_list<bool> escaped;
  };
  for (const auto& item :
       {Case{{0x09, 1, 0x3d, 0, 0x69, 0x65}, {false}},  // unobserved self-cycle
        Case{{0x09, 1, 0x60, 0, 0x3d, 0, 0x65},
             {false}},  // holds external value
        Case{{0x09, 1, 0x60, 0, 0x3d, 0, 0x3c, 0, 0x64}, {false}},
        Case{{0x09, 1, 0x61, 0, 0x60, 0, 0x64}, {true}},
        Case{{0x09, 1, 0xfc, 0, 0x64}, {true}},        // current env alias
        Case{{0x09, 1, 0x69, 0x3d, 0, 0x65}, {true}},  // captured slot
        Case{{0x09, 1, 0x61, 0, 0x60, 1, 0x2a, 0, 0, 0x65},
             {true}},                                  // call arg
        Case{{0x09, 1, 0x09, 1, 0x64}, {true, true}},  // escaped child's parent
        Case{{0x09, 1, 0x61, 0, 0x09, 1, 0x60, 0, 0x64}, {true, false}},
        Case{{0x09, 1, 0x09, 1, 0x33, 0, 0, 0, 0, 0x65}, {true, true}},
        Case{{0x09, 1, 0x3d, 0, 0x69, 0x4d, 0xfb}, {false}},  // fresh each lap
        Case{{0x09, 1, 0x61, 0, 0x69, 0x60, 1, 0x4f, 4, 0x4d, 0xf7, 0x60, 0,
              0x64},
             {true}},
        Case{{0x09, 1,    0x61, 0,    0x69, 0x09, 1,    0x61, 1,
              0x69, 0x60, 2,    0x4f, 8,    0x60, 0,    0x61, 3,
              0x4d, 6,    0x60, 1,    0x61, 3,    0x60, 3,    0x64},
             {true, true}}}) {
    const auto actual = std::get<EnvironmentEscape>(Analyze(Body(item.code)));
    std::uint32_t id = 0;
    for (const auto expected : item.escaped)
      Check(actual.MayEscape({id++}) == expected);
    Check(!actual.MayEscape({id}));
  }
  for (const auto code : {std::initializer_list<std::uint8_t>{
                              0x09, 1, 0x62, 1, 0, 0, 0, 0x1f, 0, 0x65},
                          {0x09, 1, 0x63, 5, 0, 0, 0, 0, 0, 0xff, 0x7f, 0x65},
                          {0x09, 1, 0x63, 5, 0, 0, 0, 0, 0, 0xff, 0xff, 0x65}})
    Check(std::get<EscapeError>(Analyze(Body(code))) ==
          EscapeError::kOpaqueValue);
  const auto& escaped = std::get<EnvironmentEscape>(result);
  const auto sparse = std::get<EnvironmentEscape>(
      Analyze(Body({0xfd, 2, 0xff, 0xff, 0xfd, 0x0d, 0, 0, 0xfe, 0xff, 0xfd,
                    0x0c, 0, 0, 0xfe, 0xff, 0x64})));
  Check(sparse.work.cells == 1 && sparse.MayEscape({0}) == true);
  const auto deprecated = std::get<EnvironmentEscape>(
      Analyze(Body({0x09, 1, 0x61, 0, 0, 0xfc, 0x20, 0, 0, 0x3c, 0, 0x64})));
  Check(deprecated.MayEscape({0}) == true);
  const auto distant = std::get<EnvironmentEscape>(
      Analyze(Body({0x09, 1, 0x69, 0xfd, 0x0d, 0xff, 0xff, 0xff, 0xff, 0x65})));
  Check(distant.MayEscape({0}) == true);
  for (std::uint64_t limit = 0; limit < escaped.work.units; ++limit) {
    IdfQuota limited{limit};
    Check(std::get<EscapeError>(EnvironmentEscape::Build(
              body, ssa, flow, 1, limited)) == EscapeError::kWorkLimit);
    Check(limited.Remaining() == 0);
  }
  IdfQuota exact{escaped.work.units};
  Check(std::get<EnvironmentEscape>(
            EnvironmentEscape::Build(body, ssa, flow, 1, exact,
                                     {escaped.work.peak_bytes}))
            .MayEscape({0}) == true);
  Check(exact.Remaining() == 0);
  Check(std::get<EscapeError>(EnvironmentEscape::Build(
            body, ssa, flow, 1, quota, {escaped.work.peak_bytes - 1})) ==
        EscapeError::kResourceLimit);
  Check(std::get<EscapeError>(EnvironmentEscape::Build(
            {}, ssa, flow, 1, quota)) == EscapeError::kInvalidInput);
  auto owned = std::get<EnvironmentEscape>(Analyze(Body({0x09, 1, 0x64})));
  auto moved = std::move(owned);
  Check(!owned.MayEscape({0}) && owned.AllocatedBytes() == 0);
  owned = std::get<EnvironmentEscape>(Analyze(Body({0x09, 1, 0x65})));
  owned = std::move(moved);
  Check(!moved.MayEscape({0}) && owned.MayEscape({0}) == true);
  const auto empty = std::get<EnvironmentEscape>(Analyze(Body({0x65})));
  Check(empty.AllocatedBytes() == 0 && !empty.MayEscape({0}));
  VerifyEscapeOracle();
  VerifyEscapeScaling();
}
