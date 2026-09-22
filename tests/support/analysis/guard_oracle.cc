#include <algorithm>
#include <random>

#include "abcd/analysis/guarded_reads.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
void VerifyGuardOracle() {
  using namespace analysis;
  constexpr std::size_t kBlocks = 8, kBlockBytes = 8, kRegisters = 4;
  std::mt19937 random{41702};
  for (std::size_t trial = 0; trial < 500; ++trial) {
    std::vector<std::uint8_t> code;
    for (std::size_t block = 0; block < kBlocks; ++block) {
      const auto reg = static_cast<std::uint8_t>(random() % kRegisters);
      const auto other = static_cast<std::uint8_t>(random() % kRegisters);
      if (random() % 2)
        code.insert(code.end(), {0xfe, 6, reg, other});
      else
        code.insert(code.end(), {0x45, reg, other, 0xd5});
      for (const auto opcode : {0x4f, 0x4d}) {
        const auto delta =
            static_cast<std::int32_t>(random() % kBlocks * kBlockBytes) -
            static_cast<std::int32_t>(code.size());
        code.insert(code.end(), {static_cast<std::uint8_t>(opcode),
                                 static_cast<std::uint8_t>(delta)});
      }
    }
    const auto body = Body(code);
    const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
    IdfQuota quota{1000000};
    const auto ssa = std::get<RegisterSsa>(
        RegisterSsa::Build(body, graph, kRegisters, quota));
    const auto flow = std::get<RegisterValueFlow>(
        RegisterValueFlow::Build(body, ssa, kRegisters, {}, quota));
    const auto actual = std::get<GuardedReads>(
        GuardedReads::Build(body, graph, ssa, flow, kRegisters, quota));
    const auto count = body.instructions.size();
    // Independent instruction-level paths from decoded jumps. Removing the
    // guard's node must disconnect a dominated later read from method entry.
    // Full graph scans per candidate are deliberately small test-oracle work.
    const auto reach_without = [&](std::size_t removed) {
      std::vector<bool> reached(count);
      std::vector<std::size_t> pending;
      if (removed != 0) {
        reached[0] = true;
        pending.push_back(0);
      }
      for (std::size_t head = 0; head < pending.size(); ++head) {
        const auto pc = pending[head];
        const auto& item = body.instructions[pc];
        const auto append = [&](std::size_t next) {
          if (next == removed || next >= count || reached[next]) return;
          reached[next] = true;
          pending.push_back(next);
        };
        if (item.branch_target) {
          const auto found =
              std::ranges::find_if(body.instructions, [&](const auto& target) {
                return target.offset.value == item.branch_target->value;
              });
          Check(found != body.instructions.end());
          append(static_cast<std::size_t>(found - body.instructions.begin()));
        }
        if (item.decoded.opcode != abc::Opcode::kJMP_IMM8) append(pc + 1);
      }
      return reached;
    };
    const auto reachable = reach_without(count);
    std::vector<std::vector<bool>> bypass(count);
    for (std::size_t i = 0; i < count; ++i)
      if (reachable[i] && body.instructions[i].decoded.opcode ==
                              abc::Opcode::kTHROW_UNDEFINEDIFHOLE_PREF_V8_V8)
        bypass[i] = reach_without(i);
    for (std::uint32_t i = 0; i < count; ++i) {
      if (!reachable[i]) continue;
      for (const auto& input : ssa.Reads({i})) {
        bool guarded = false;
        for (std::uint32_t guard = 0; guard < count; ++guard) {
          if (guard == i || bypass[guard].empty()) continue;
          guarded |=
              !bypass[guard][i] && ssa.Reads({guard})[0].value == input.value;
        }
        const auto read = actual.At({i}, input.ordinal);
        Check(read.has_value() && read->read.value == input.value);
        Check(read->guard.has_value() == guarded);
        if (guarded) {
          const auto guard = read->guard->value;
          Check(guard != i && !bypass[guard].empty() && !bypass[guard][i]);
          Check(ssa.Reads({guard})[0].value == input.value);
        }
        const auto expected = guarded ? ValueKinds::Language()
                                            .Join(ValueKinds::Environment())
                                            .Join(ValueKinds::OtherInternal())
                                      : ValueKinds::Unknown();
        Check(read->kinds == expected);
      }
    }
  }
}
}  // namespace abcd::test
