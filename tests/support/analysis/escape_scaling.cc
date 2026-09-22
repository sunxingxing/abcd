#include "abcd/analysis/environment_escape.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
void VerifyEscapeScaling() {
  using namespace analysis;
  for (const std::uint32_t count : {200U, 201U, 4096U, 65536U}) {
    for (const bool escape : {false, true}) {
      std::vector<std::uint8_t> bytes;
      for (std::uint32_t i = 0; i < count; ++i)
        bytes.insert(bytes.end(), {0x09, 1, 0x3d, 0});
      bytes.push_back(escape ? 0x64 : 0x65);
      const auto body = Body(bytes);
      const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
      IdfQuota quota{UINT64_MAX};
      const auto ssa =
          std::get<RegisterSsa>(RegisterSsa::Build(body, graph, 0, quota));
      const auto flow =
          std::get<EnvironmentFlow>(AnalyzeEnvironments(body, graph, 0));
      const auto result = std::get<EnvironmentEscape>(
          EnvironmentEscape::Build(body, ssa, flow, 0, quota));
      for (std::uint32_t i = 0; i < count; ++i)
        Check(result.MayEscape({i}) == escape);
      Check(result.work.cells == count);
      Check(result.work.edge_visits <= std::uint64_t(count) * 2);
      Check(result.work.units < std::uint64_t(count) * 300);
    }
  }
}
}  // namespace abcd::test
