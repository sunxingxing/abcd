#include "abcd/analysis/register_uses.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <random>
#include <source_location>

namespace {
using namespace abcd;
void Check(bool value,
           std::source_location where = std::source_location::current()) {
  if (!value) {
    std::cerr << where.line() << '\n';
    std::abort();
  }
}
abc::MethodBody Body(std::span<const std::uint8_t> bytes) {
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded =
        std::get<abc::DecodedInstruction>(abc::DecodeInstruction(
            bytes.subspan(pc), abc::ProfileId::kDynamic1206));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta)
      instruction.branch_target = abc::CodeOffset{
          static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction);
    pc += decoded.size;
  }
  return body;
}
abc::MethodBody Body(std::initializer_list<std::uint8_t> bytes) {
  return Body(std::span(bytes.begin(), bytes.size()));
}
}  // namespace
int main() {
  using namespace analysis;
  const auto body =
      Body({0x60, 0,    0x4f, 11, 0x62, 11, 0,    0, 0,    0x61, 1,   0x4d,
            9,    0x62, 22,   0,  0,    0,  0x61, 1, 0x60, 1,    0x64});
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  IdfQuota quota{10000};
  const auto uses =
      std::get<RegisterUses>(RegisterUses::Build(body, graph, 2, quota));
  const auto id = *uses.Find(abc::RegisterId{1});
  const std::array<BlockId, 2> expected{{{1}, {2}}};
  Check(std::ranges::equal(uses.Definitions(id), expected));
  Check(uses.UpwardUses(id).size() == 1 && uses.UpwardUses(id)[0].value == 3);
  const auto index = std::get<IdfIndex>(IdfIndex::Build(graph));
  const auto live = std::get<BindingLiveness>(
      index.LiveIn(uses.Definitions(id), uses.UpwardUses(id), quota));
  const auto placed = std::get<PhiPlacement>(
      index.Place(uses.Definitions(id), quota, live.Blocks()));
  Check(placed.Blocks().size() == 1 && placed.Blocks()[0].value == 3);
  Check(uses.UpwardUses(*uses.Find(Accumulator{})).empty());
  Check(!uses.Find(abc::RegisterId{2}));
  Check(uses.Definitions({SIZE_MAX}).empty() &&
        uses.UpwardUses({SIZE_MAX}).empty());
  const auto run = [](const abc::MethodBody& input, std::uint64_t frame = 4) {
    IdfQuota work{UINT64_MAX};
    return std::get<RegisterUses>(RegisterUses::Build(
        input, std::get<NormalFlow>(BuildNormalFlow(input)), frame, work));
  };
  auto self = run(Body({0x45, 2, 2, 0x65}));
  Check(self.Definitions(*self.Find(abc::RegisterId{2})).size() == 1);
  Check(self.UpwardUses(*self.Find(abc::RegisterId{2})).size() == 1);
  auto moved = std::move(self);
  Check(self.Bindings().empty() && self.AllocatedBytes() == 0 &&
        !self.Find(abc::RegisterId{2}));
  Check(self.Definitions({0}).empty() && self.UpwardUses({0}).empty());
  self = run(Body({0x64}));
  self = std::move(moved);
  Check(self.Find(abc::RegisterId{2}) && !moved.Find(abc::RegisterId{2}));
  Check(self.UpwardUses(*self.Find(abc::RegisterId{2}))[0].value == 0);
  // Each temporary body/CFG above is already destroyed: results own all lists.
  auto legacy = run(Body({0xfc, 0x20, 0, 2, 0x64}));
  Check(legacy.Definitions(*legacy.Find(Accumulator{})).empty());
  Check(legacy.UpwardUses(*legacy.Find(abc::RegisterId{2})).size() == 1);
  Check(legacy.UpwardUses(*legacy.Find(Accumulator{})).size() == 1);
  auto diagnostic = run(Body({0xfe, 6, 2, 3, 0x65}));
  Check(diagnostic.UpwardUses(*diagnostic.Find(abc::RegisterId{3})).size() ==
        1);
  auto range = run(Body({0x73, 0, 255, 255, 0x64}), 510);
  Check(range.Bindings().size() == 256);
  for (std::uint32_t reg = 255; reg < 510; ++reg)
    Check(range.UpwardUses(*range.Find(abc::RegisterId{reg})).size() == 1);
  Check(!range.Find(abc::RegisterId{254}) && !range.Find(abc::RegisterId{510}));
  const auto sparse = run(body, UINT64_MAX);
  Check(sparse.AllocatedBytes() == uses.AllocatedBytes());
  Check(sparse.work.peak_bytes == uses.work.peak_bytes);
  auto loop = run(Body({0x60, 0, 0x61, 1, 0x4d, 0xfc}));
  Check(loop.UpwardUses(*loop.Find(abc::RegisterId{0})).size() == 1);
  Check(loop.Definitions(*loop.Find(abc::RegisterId{1})).size() == 1);
  const auto carried_body =
      Body({0x60, 0, 0x61, 1, 0x4d, 2,    0x60, 1,    0x4f, 11,
            0x62, 1, 0,    0, 0,    0x61, 1,    0x4d, 0xf5, 0x65});
  const auto carried_graph =
      std::get<NormalFlow>(BuildNormalFlow(carried_body));
  IdfQuota carried_quota{10000};
  const auto carried = std::get<RegisterUses>(
      RegisterUses::Build(carried_body, carried_graph, 2, carried_quota));
  const auto carried_id = *carried.Find(abc::RegisterId{1});
  const auto carried_index = std::get<IdfIndex>(IdfIndex::Build(carried_graph));
  const auto carried_live = std::get<BindingLiveness>(
      carried_index.LiveIn(carried.Definitions(carried_id),
                           carried.UpwardUses(carried_id), carried_quota));
  const auto carried_phi = std::get<PhiPlacement>(carried_index.Place(
      carried.Definitions(carried_id), carried_quota, carried_live.Blocks()));
  Check(carried_phi.Blocks().size() == 1 && carried_phi.Blocks()[0].value == 1);
  // An unknown opcode after return is normal-unreachable, not a static ABC.
  Check(run(Body({0x65, 0x26, 0, 0, 0x65})).Bindings().empty());
  const auto failure = [&](const abc::MethodBody& input,
                           const NormalFlow& flow) {
    IdfQuota work{UINT64_MAX};
    return std::get<RegisterUseFailure>(
        RegisterUses::Build(input, flow, 2, work));
  };
  auto unsupported = Body({0, 0x26, 0, 0, 0x65});
  auto rejected =
      failure(unsupported, std::get<NormalFlow>(BuildNormalFlow(unsupported)));
  Check(rejected.kind == RegisterUseError::kInstruction &&
        rejected.offset.value == 1 &&
        rejected.instruction == ScalarError::kUnsupportedOpcode);
  auto malformed = body;
  malformed.instructions[0].decoded.operand_count = 0;
  Check(failure(malformed, graph).instruction ==
        ScalarError::kMalformedInstruction);
  malformed = body;
  malformed.instructions[0].decoded.operands[0].raw_bits = 2;
  Check(failure(malformed, graph).instruction == ScalarError::kRegisterRange);
  malformed = body;
  malformed.tries.push_back({});
  Check(failure(malformed, graph).kind == RegisterUseError::kExceptionRegions);
  malformed = body;
  malformed.handlers.push_back({});
  Check(failure(malformed, graph).kind == RegisterUseError::kExceptionRegions);
  auto invalid = graph;
  invalid.blocks[0].successors[0].kind = FlowEdgeKind::kResume;
  Check(failure(body, invalid).kind == RegisterUseError::kResumeEdge);
  invalid.blocks[0].successors[0].kind = static_cast<FlowEdgeKind>(UINT8_MAX);
  Check(failure(body, invalid).kind == RegisterUseError::kInvalidGraph);
  invalid = graph;
  invalid.blocks[0].successors[0].target.value = UINT32_MAX;
  Check(failure(body, invalid).kind == RegisterUseError::kInvalidGraph);
  invalid = graph;
  invalid.blocks[0].instruction_count = UINT32_MAX;
  Check(failure(body, invalid).kind == RegisterUseError::kInvalidGraph);
  invalid = graph;
  invalid.blocks[0].successor_count = UINT8_MAX;
  Check(failure(body, invalid).kind == RegisterUseError::kInvalidGraph);
  invalid = graph;
  invalid.blocks[1].first_instruction = 0;
  Check(failure(body, invalid).kind == RegisterUseError::kInvalidGraph);
  Check(failure(body, {}).kind == RegisterUseError::kInvalidGraph);
  for (const auto bytes : {std::size_t{0}, uses.work.peak_bytes - 1}) {
    IdfQuota work{UINT64_MAX};
    Check(std::get<RegisterUseFailure>(
              RegisterUses::Build(body, graph, 2, work, {bytes}))
              .kind == RegisterUseError::kResourceLimit);
    Check(work.Consumed() > 0);
  }
  IdfQuota exact{uses.work.units};
  Check(std::get<RegisterUses>(
            RegisterUses::Build(body, graph, 2, exact, {uses.work.peak_bytes}))
            .AllocatedBytes() == uses.AllocatedBytes());
  Check(exact.Remaining() == 0);
  Check(std::get<IdfError>(
            index.LiveIn(uses.Definitions(id), uses.UpwardUses(id), exact)) ==
        IdfError::kWorkLimit);
  IdfQuota short_work{uses.work.units - 1};
  Check(std::get<RegisterUseFailure>(
            RegisterUses::Build(body, graph, 2, short_work))
            .kind == RegisterUseError::kWorkLimit);
  Check(short_work.Remaining() == 0);
  IdfQuota pipeline{quota.Consumed()};
  auto integrated =
      std::get<RegisterUses>(RegisterUses::Build(body, graph, 2, pipeline));
  auto incoming = std::get<BindingLiveness>(index.LiveIn(
      integrated.Definitions(id), integrated.UpwardUses(id), pipeline));
  Check(std::get<PhiPlacement>(index.Place(integrated.Definitions(id), pipeline,
                                           incoming.Blocks()))
            .Blocks()
            .size() == 1);
  Check(pipeline.Remaining() == 0);
  // Independent small oracle: manually generated MOV operands, first access
  // per register determines upward use. No effect-lift reuse in the oracle.
  std::mt19937 random{91827};
  for (std::size_t trial = 0; trial < 1000; ++trial) {
    constexpr std::size_t kRegisters = 16;
    std::array<bool, kRegisters> defined{}, upward{};
    std::vector<std::uint8_t> code;
    for (std::size_t i = 0; i < 50; ++i) {
      const auto dst = static_cast<std::uint8_t>(random() % kRegisters);
      const auto src = static_cast<std::uint8_t>(random() % kRegisters);
      code.insert(code.end(), {0x45, dst, src});
      if (!defined[src]) upward[src] = true;
      defined[dst] = true;
    }
    code.push_back(0x65);
    const auto actual = run(Body(code), kRegisters);
    for (std::uint32_t reg = 0; reg < kRegisters; ++reg) {
      const auto binding = actual.Find(abc::RegisterId{reg});
      Check(binding.has_value() == (defined[reg] || upward[reg]));
      if (binding) {
        Check(actual.Definitions(*binding).size() == (defined[reg] ? 1 : 0));
        Check(actual.UpwardUses(*binding).size() == (upward[reg] ? 1 : 0));
      }
    }
  }
  for (const std::size_t count : {200, 201, 4096, 65536}) {
    std::vector<std::uint8_t> code;
    for (std::size_t i = 0; i < count; ++i) {
      const auto low = static_cast<std::uint8_t>(i & UINT8_MAX);
      const auto high = static_cast<std::uint8_t>(i >> 8);
      code.insert(code.end(), {0x8f, low, high, low, high});
    }
    code.push_back(0x65);
    const auto actual = run(Body(code), UINT64_MAX);
    Check(actual.Bindings().size() == count &&
          actual.work.instructions == count + 1);
    Check(actual.work.accesses == count * 2);
    Check(actual.AllocatedBytes() ==
          count * (sizeof(RegisterBinding) + 2 * sizeof(BlockId)));
    Check(actual.work.units < count * 100);
    IdfQuota interrupted{actual.work.units / 2};
    const auto input = Body(code);
    const auto flow = std::get<NormalFlow>(BuildNormalFlow(input));
    Check(std::get<RegisterUseFailure>(
              RegisterUses::Build(input, flow, UINT64_MAX, interrupted))
              .kind == RegisterUseError::kWorkLimit);
    Check(interrupted.Remaining() == 0);
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto binding = *actual.Find(abc::RegisterId{i});
      Check(actual.Definitions(binding).size() == 1 &&
            actual.UpwardUses(binding).size() == 1);
    }
  }
}
