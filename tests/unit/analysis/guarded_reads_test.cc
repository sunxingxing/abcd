#include "abcd/analysis/guarded_reads.h"

#include "analysis/ssa_fixture.h"

using namespace abcd;
using namespace analysis;
using namespace abcd::test;

namespace abcd::test {
void VerifyGuardOracle();
void VerifyGuardScaling();
}  // namespace abcd::test
namespace {
GuardedReads Analyze(const abc::MethodBody& body) {
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  IdfQuota quota{UINT64_MAX};
  const auto ssa =
      std::get<RegisterSsa>(RegisterSsa::Build(body, graph, 4, quota));
  const auto flow = std::get<RegisterValueFlow>(
      RegisterValueFlow::Build(body, ssa, 4, {}, quota));
  return std::get<GuardedReads>(
      GuardedReads::Build(body, graph, ssa, flow, 4, quota));
}
}  // namespace
int main() {
  // Guard v0 with diagnostic name v1, then read the same value into acc.
  const auto body = Body({0xfe, 6, 0, 1, 0x60, 0, 0x64});
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  IdfQuota quota{UINT64_MAX};
  const auto ssa =
      std::get<RegisterSsa>(RegisterSsa::Build(body, graph, 2, quota));
  const auto value = ssa.Reads({0})[0].value;
  const auto mixed = ValueKinds::Language().Join(ValueKinds::Hole());
  const std::array<EntryValueFact, 1> entries{{{value, mixed}}};
  const auto flow = std::get<RegisterValueFlow>(
      RegisterValueFlow::Build(body, ssa, 2, entries, quota));
  const auto reads = std::get<GuardedReads>(
      GuardedReads::Build(body, graph, ssa, flow, 2, quota));
  Check(reads.At({0}, 0)->kinds == mixed);
  Check(!reads.At({0}, 0)->guard);
  Check(reads.At({1}, 0)->kinds == ValueKinds::Language());
  Check(reads.At({1}, 0)->guard == InstructionId{0});
  Check(flow.At(value) == mixed);
  // No copy-equivalence inference: the return reads a new SSA accumulator.
  Check(reads.At({2}, 0)->kinds == mixed);
  Check(!reads.At({2}, 0)->guard);
  Check(!reads.At({UINT32_MAX}, 0) && !reads.At({0}, UINT16_MAX));
  Check(!reads.At({0}, 1)->guard);
  Check(reads.At({0}, 1)->kinds == ValueKinds::Unknown());
  const std::array atoms{ValueKinds::Language(), ValueKinds::Hole(),
                         ValueKinds::Environment(),
                         ValueKinds::OtherInternal()};
  for (std::uint32_t mask = 1; mask < (1U << kValueKindCount); ++mask) {
    ValueKinds kinds, filtered;
    for (std::size_t i = 0; i < atoms.size(); ++i) {
      if (!(mask & (1U << i))) continue;
      kinds = kinds.Join(atoms[i]);
      if (i != 1) filtered = filtered.Join(atoms[i]);
    }
    const std::array<EntryValueFact, 1> entry{{{value, kinds}}};
    const auto types = std::get<RegisterValueFlow>(
        RegisterValueFlow::Build(body, ssa, 2, entry, quota));
    const auto refined = std::get<GuardedReads>(
        GuardedReads::Build(body, graph, ssa, types, 2, quota));
    Check(refined.At({0}, 0)->kinds == kinds);
    Check(refined.At({1}, 0)->kinds == filtered);
    Check(types.At(value) == kinds);
  }
  // Same-block reassignment creates a different value. A diagnostic-name read
  // is not a check of the name. The last successful same-value guard wins.
  const auto names = Analyze(Body({0xfe, 6, 0, 1, 0x60, 1, 0xfe, 6, 0, 1, 0x60,
                                   0, 0x61, 0, 0x60, 0, 0x64}));
  Check(!names.At({1}, 0)->guard);
  Check(names.At({2}, 0)->guard == InstructionId{0});
  Check(!names.At({2}, 1)->guard);
  Check(names.At({3}, 0)->guard == InstructionId{2});
  Check(!names.At({5}, 0)->guard);
  const auto impossible = Analyze(Body({0x70, 0xfe, 9, 0, 0, 0x64}));
  Check(impossible.At({1}, 0)->kinds == ValueKinds::Hole());
  Check(impossible.At({2}, 0)->kinds.IsEmpty());
  Check(!impossible.At({2}, 0)->kinds.IsLanguage());
  // Guarded arm, unguarded arm, then join. No proof leaks across siblings.
  const auto bypass = Analyze(Body(
      {0x4f, 10, 0xfe, 6, 0, 1, 0x60, 0, 0x4d, 4, 0x60, 0, 0x60, 0, 0x64}));
  Check(bypass.At({2}, 0)->guard == InstructionId{1});
  Check(!bypass.At({4}, 0)->guard && !bypass.At({5}, 0)->guard);
  // Nested arm guard must restore the earlier common guard at the sibling.
  const auto nested = Analyze(Body({0xfe, 6, 0, 1, 0x4f, 10, 0xfe, 6, 0, 1,
                                    0x60, 0, 0x4d, 4, 0x60, 0, 0x60, 0, 0x64}));
  Check(nested.At({3}, 0)->guard == InstructionId{2});
  Check(nested.At({5}, 0)->guard == InstructionId{0});
  Check(nested.At({6}, 0)->guard == InstructionId{0});
  // Entry is a loop header: read/guard/read, overwrite, backedge. Header Phi
  // selects a new dynamic value each iteration; no previous-iteration proof.
  const auto loop = Analyze(
      Body({0x60, 0, 0xfe, 6, 0, 1, 0x60, 0, 0x70, 0x61, 0, 0x4d, 0xf5}));
  Check(!loop.At({0}, 0)->guard && !loop.At({1}, 0)->guard);
  Check(loop.At({2}, 0)->guard == InstructionId{1});
  auto owned = Analyze(Body({0xfe, 9, 0, 0, 0x64}));
  auto moved = std::move(owned);
  Check(owned.Reads().empty() && !owned.At({0}, 0) &&
        owned.AllocatedBytes() == 0);
  owned = Analyze(Body({0x64}));
  owned = std::move(moved);
  Check(moved.Reads().empty());
  Check(owned.At({1}, 0)->guard == InstructionId{0});
  for (std::uint64_t limit = 0; limit < reads.work.units; ++limit) {
    IdfQuota limited{limit};
    Check(std::get<GuardReadError>(
              GuardedReads::Build(body, graph, ssa, flow, 2, limited)) ==
          GuardReadError::kWorkLimit);
    Check(limited.Remaining() == 0);
  }
  IdfQuota exact{reads.work.units};
  Check(std::get<GuardedReads>(GuardedReads::Build(body, graph, ssa, flow, 2,
                                                   exact,
                                                   {reads.work.peak_bytes}))
            .AllocatedBytes() == reads.AllocatedBytes());
  Check(exact.Remaining() == 0);
  Check(std::get<GuardReadError>(GuardedReads::Build(
            body, graph, ssa, flow, 2, quota, {reads.work.peak_bytes - 1})) ==
        GuardReadError::kResourceLimit);
  auto bad_graph = graph;
  bad_graph.blocks[0].instruction_count++;
  Check(std::get<GuardReadError>(
            GuardedReads::Build(body, bad_graph, ssa, flow, 2, quota)) ==
        GuardReadError::kInvalidGraph);
  bad_graph = graph;
  bad_graph.blocks[0].successor_count = 1;
  bad_graph.blocks[0].successors[0] = {{0}, FlowEdgeKind::kResume};
  Check(std::get<GuardReadError>(
            GuardedReads::Build(body, bad_graph, ssa, flow, 2, quota)) ==
        GuardReadError::kResumeEdge);
  auto bad_body = body;
  bad_body.handlers.emplace_back();
  Check(std::get<GuardReadError>(
            GuardedReads::Build(bad_body, graph, ssa, flow, 2, quota)) ==
        GuardReadError::kExceptionRegions);
  bad_body = body;
  bad_body.instructions[0].decoded.operand_count = 0;
  Check(std::get<GuardReadError>(
            GuardedReads::Build(bad_body, graph, ssa, flow, 2, quota)) ==
        GuardReadError::kInstruction);
  VerifyGuardOracle();
  VerifyGuardScaling();
}
