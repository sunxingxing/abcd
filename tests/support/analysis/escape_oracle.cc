#include <random>

#include "abcd/analysis/environment_escape.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
void VerifyEscapeOracle() {
  using namespace analysis;
  constexpr std::uint8_t kEnvironments = 7, kSlots = 7;
  constexpr std::size_t kNodes = kEnvironments * (kSlots + 1);
  std::mt19937 random{81920};
  for (std::size_t trial = 0; trial < 500; ++trial) {
    std::vector<std::uint8_t> code;
    std::array<std::vector<std::size_t>, kNodes> edges;
    const auto cell = [](std::uint8_t owner, std::uint8_t slot) {
      return std::size_t(kEnvironments) + owner * kSlots + slot;
    };
    for (std::uint8_t i = 0; i < kEnvironments; ++i) {
      code.insert(code.end(), {0x09, kSlots, 0x61, i});
      if (i) edges[i].push_back(i - 1);
      for (std::uint8_t slot = 0; slot < kSlots; ++slot)
        edges[i].push_back(cell(i, slot));
    }
    // Model alias constraints as ordinary bidirectional graph edges, not
    // union-find. Containment stays directional; BFS is an independent oracle.
    for (std::size_t store = 0; store < kEnvironments * 2; ++store) {
      const auto owner = static_cast<std::uint8_t>(random() % kEnvironments);
      const auto slot = static_cast<std::uint8_t>(random() % kSlots);
      const auto value = static_cast<std::uint8_t>(random() % kEnvironments);
      constexpr std::uint8_t kSlotShift = 4;
      const auto address = static_cast<std::uint8_t>(
          (slot << kSlotShift) | (kEnvironments - 1 - owner));
      code.insert(code.end(), {0x60, value, 0x3d, address});
      edges[cell(owner, slot)].push_back(value);
      edges[value].push_back(cell(owner, slot));
    }
    const auto returned = static_cast<std::uint8_t>(random() % kEnvironments);
    code.insert(code.end(), {0x60, returned, 0x64});
    const auto body = Body(code);
    const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
    IdfQuota quota{1000000};
    const auto ssa = std::get<RegisterSsa>(
        RegisterSsa::Build(body, graph, kEnvironments, quota));
    const auto flow = std::get<EnvironmentFlow>(
        AnalyzeEnvironments(body, graph, kEnvironments));
    const auto actual = std::get<EnvironmentEscape>(
        EnvironmentEscape::Build(body, ssa, flow, kEnvironments, quota));
    std::array<bool, kNodes> seen{};
    std::vector<std::size_t> pending{returned};
    seen[returned] = true;
    for (std::size_t head = 0; head < pending.size(); ++head)
      for (const auto next : edges[pending[head]])
        if (!seen[next]) {
          seen[next] = true;
          pending.push_back(next);
        }
    for (std::uint32_t i = 0; i < kEnvironments; ++i)
      Check(actual.MayEscape({i}) == seen[i]);
    Check(actual.work.edge_visits <= kEnvironments + actual.work.cells);
  }
}
}  // namespace abcd::test
