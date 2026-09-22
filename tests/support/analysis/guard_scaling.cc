#include "abcd/analysis/guarded_reads.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
void VerifyGuardScaling() {
  using namespace analysis;
  for (const std::uint32_t count : {200U, 201U, 4096U, 65536U}) {
    for (const bool deep : {false, true}) {
      std::vector<std::uint8_t> code;
      for (std::uint32_t i = 0; i < count; ++i) {
        code.insert(code.end(), {0xfe, 6, 0, 1, 0x60, 0});
        if (deep) code.insert(code.end(), {0x4d, 2});
      }
      code.push_back(0x64);
      const auto body = Body(code);
      const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
      IdfQuota quota{UINT64_MAX};
      const auto ssa =
          std::get<RegisterSsa>(RegisterSsa::Build(body, graph, 2, quota));
      const auto flow = std::get<RegisterValueFlow>(
          RegisterValueFlow::Build(body, ssa, 2, {}, quota));
      const auto reads = std::get<GuardedReads>(
          GuardedReads::Build(body, graph, ssa, flow, 2, quota));
      Check(reads.work.guards == count);
      Check(reads.work.instructions == body.instructions.size());
      Check(reads.Reads().size() == std::size_t(count) * 3 + 1);
      Check(reads.work.units < 100 * reads.Reads().size());
      const std::uint32_t stride = deep ? 3 : 2;
      for (std::uint32_t i = 0; i < count; ++i) {
        Check(reads.At({stride * i + 1}, 0)->guard ==
              InstructionId{stride * i});
        Check(reads.At({stride * i}, 0)->guard ==
              (i == 0 ? std::optional<InstructionId>{}
                      : InstructionId{stride * (i - 1)}));
      }
    }
  }
}
}  // namespace abcd::test
