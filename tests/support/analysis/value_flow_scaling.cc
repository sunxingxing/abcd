#include "abcd/analysis/register_value_flow.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
void VerifyValueScaling() {
  using namespace analysis;
  for (const std::size_t count : {200, 201, 4096, 65536}) {
    std::vector<std::uint8_t> code{0x70};
    for (std::size_t i = 0; i < count; ++i)
      code.insert(code.end(), {0x61, 0, 0x60, 0});
    code.push_back(0x64);
    {
      const auto body = Body(code);
      IdfQuota quota{UINT64_MAX};
      const auto ssa = std::get<RegisterSsa>(RegisterSsa::Build(
          body, std::get<NormalFlow>(BuildNormalFlow(body)), 1, quota));
      const auto flow = std::get<RegisterValueFlow>(
          RegisterValueFlow::Build(body, ssa, 1, {}, quota));
      Check(flow.At(*ssa.Write({static_cast<std::uint32_t>(2 * count)})) ==
            ValueKinds::Hole());
      Check(flow.work.dependencies == 2 * count);
      Check(flow.work.edge_visits == 2 * count);
      Check(flow.work.units < 100 * ssa.Values().size());
    }
    // Each choice has a Hole-writing leaf jumping to the same join. The final
    // fallthrough writes a language value. Phi fan-in grows with count.
    code.clear();
    constexpr std::uint32_t kChoiceBytes = 12, kFallbackBytes = 3;
    const auto join =
        static_cast<std::uint32_t>(count * kChoiceBytes + kFallbackBytes);
    for (std::size_t i = 0; i < count; ++i) {
      code.insert(code.end(), {0x60, 0, 0x4f, 10, 0x70, 0x61, 1});
      const auto delta = join - static_cast<std::uint32_t>(code.size());
      code.push_back(0x98);
      for (std::size_t byte = 0; byte < sizeof(delta); ++byte)
        code.push_back(static_cast<std::uint8_t>(delta >> (byte * 8)));
    }
    code.insert(code.end(), {0, 0x61, 1, 0x60, 1, 0x64});
    const auto body = Body(code);
    IdfQuota quota{UINT64_MAX};
    // Input/SSA construction is separately admitted; this is not a value-flow
    // budget enlargement. Large fixture bodies are retained during the query.
    constexpr IdfBudget kFixtureSsaBudget{256 * 1024 * 1024};
    const auto ssa = std::get<RegisterSsa>(
        RegisterSsa::Build(body, std::get<NormalFlow>(BuildNormalFlow(body)), 2,
                           quota, kFixtureSsaBudget));
    const InstructionId join_load{static_cast<std::uint32_t>(5 * count + 2)};
    const auto phi = ssa.Reads(join_load)[0].value;
    Check(ssa.Inputs(phi).size() == count + 1);
    const auto flow = std::get<RegisterValueFlow>(
        RegisterValueFlow::Build(body, ssa, 2, {}, quota));
    Check(flow.At(phi) == ValueKinds::Language().Join(ValueKinds::Hole()));
    Check(flow.At(*ssa.Write(join_load)) == flow.At(phi));
    Check(flow.work.changes <= kValueKindCount * ssa.Values().size());
    Check(flow.work.edge_visits <= kValueKindCount * flow.work.dependencies);
    Check(flow.work.units < 100 * ssa.Values().size());
  }
}
}  // namespace abcd::test
