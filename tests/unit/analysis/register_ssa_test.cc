#include "analysis/ssa_fixture.h"

using namespace abcd;
using namespace analysis;
using namespace abcd::test;
int main() {
  const auto body =
      Body({0x60, 0,    0x4f, 11, 0x62, 11, 0,    0, 0,    0x61, 1,   0x4d,
            9,    0x62, 22,   0,  0,    0,  0x61, 1, 0x60, 1,    0x64});
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  IdfQuota quota{100000};
  const auto ssa =
      std::get<RegisterSsa>(RegisterSsa::Build(body, graph, 2, quota));
  const auto read = ssa.Reads({7});
  Check(read.size() == 1);
  const auto& value = ssa.Values()[read[0].value.value];
  Check(std::get<PhiValue>(value.origin).block.value == 3);
  Check(std::get<abc::RegisterId>(value.slot).value == 1);
  const auto inputs = ssa.Inputs(read[0].value);
  Check(ssa.work.phis == 1);
  Check(inputs.size() == 2);
  Check(inputs[0].value == *ssa.Write({3}));
  Check(inputs[1].value == *ssa.Write({6}));
  Check(std::get<SsaEdge>(inputs[0].edge).source.value == 1);
  Check(std::get<SsaEdge>(inputs[1].edge).source.value == 2);
  const auto build = [](const abc::MethodBody& input, std::uint64_t frame = 4) {
    IdfQuota work{UINT64_MAX};
    return std::get<RegisterSsa>(RegisterSsa::Build(
        input, std::get<NormalFlow>(BuildNormalFlow(input)), frame, work));
  };
  auto copy = build(Body({0x45, 1, 1, 0x45, 1, 1, 0x65}));
  Check(std::holds_alternative<MethodEntry>(
      copy.Values()[copy.Reads({0})[0].value.value].origin));
  Check(copy.Reads({1})[0].value == *copy.Write({0}));
  auto owned = std::move(copy);
  Check(copy.Values().empty() && copy.Reads({0}).empty() &&
        copy.Inputs({0}).empty());
  Check(copy.AllocatedBytes() == 0 && !copy.Write({0}));
  copy = build(Body({0x64}));
  copy = std::move(owned);
  Check(copy.Reads({1})[0].value == *copy.Write({0}) && owned.Values().empty());
  Check(copy.Reads({UINT32_MAX}).empty() && !copy.Write({UINT32_MAX}) &&
        copy.Inputs({UINT32_MAX}).empty());
  // Phi at method entry has BOTH initial invocation and backedge inputs.
  const auto entry_loop = build(Body({0x45, 1, 1, 0x4d, 0xfd}));
  const auto entry_phi = entry_loop.Reads({0})[0].value;
  Check(std::get<PhiValue>(entry_loop.Values()[entry_phi.value].origin)
            .block.value == 0);
  const auto entry_inputs = entry_loop.Inputs(entry_phi);
  Check(entry_inputs.size() == 2 &&
        std::holds_alternative<MethodEntry>(entry_inputs[0].edge));
  Check(std::holds_alternative<MethodEntry>(
      entry_loop.Values()[entry_inputs[0].value.value].origin));
  Check(entry_inputs[1].value == *entry_loop.Write({0}));
  // JEQZ target == fallthrough: two incoming edge ordinals, not one block key.
  const auto duplicate =
      build(Body({0x45, 1, 1, 0x4f, 2, 0x45, 1, 1, 0x4d, 0xfd}));
  const auto duplicate_phi = duplicate.Reads({2})[0].value;
  const auto duplicate_inputs = duplicate.Inputs(duplicate_phi);
  Check(duplicate_inputs.size() == 3);
  Check(std::get<SsaEdge>(duplicate_inputs[0].edge).source.value == 0);
  Check(std::get<SsaEdge>(duplicate_inputs[1].edge).source.value == 0);
  Check(std::get<SsaEdge>(duplicate_inputs[0].edge).successor == 0);
  Check(std::get<SsaEdge>(duplicate_inputs[1].edge).successor == 1);
  Check(duplicate_inputs[0].value == duplicate_inputs[1].value);
  // Two-entry SCC: neither cyclic block dominates the other.
  const auto irreducible = build(Body(
      {0x60, 0, 0x4f, 7, 0x45, 1, 0, 0x4d, 2, 0x45, 0, 1, 0x4f, 0xf8, 0x64}));
  const auto left_phi = irreducible.Reads({2})[0].value;
  const auto right_phi = irreducible.Reads({4})[0].value;
  Check(std::get<PhiValue>(irreducible.Values()[left_phi.value].origin)
            .block.value == 1);
  Check(std::get<PhiValue>(irreducible.Values()[right_phi.value].origin)
            .block.value == 2);
  Check(irreducible.Inputs(left_phi).size() == 2 &&
        irreducible.Inputs(right_phi).size() == 2);
  Check(irreducible.Inputs(left_phi)[1].value == *irreducible.Write({4}));
  Check(irreducible.Inputs(right_phi)[1].value == *irreducible.Write({2}));
  const auto range = build(Body({0x73, 0, 255, 255, 0x64}), 510);
  Check(range.Reads({0}).size() == 256 && range.Reads({0})[255].ordinal == 255);
  Check(std::get<abc::RegisterId>(
            range.Values()[range.Reads({0})[255].value.value].slot)
            .value == 509);
  Check(range.Reads({1})[0].value == *range.Write({0}));
  // A compact range plus fixed inputs can exceed the encoded range width.
  // Read occurrences must remain distinct even when their slots are equal.
  {
    constexpr std::uint16_t kLargestEncodedRange = UINT16_MAX;
    const auto flow = std::get<NormalFlow>(BuildNormalFlow(Body({0x65})));
    RegisterSite site{};
    site.effect.read_count = 2;
    site.effect.reads[0] = Accumulator{};
    site.effect.reads[1] = abc::RegisterId{0};
    site.effect.range = CallRange{{0}, kLargestEncodedRange};
    IdfQuota work{UINT64_MAX};
    const auto large = std::get<RegisterSsa>(RegisterSsa::BuildSites(
        std::span(&site, 1), flow, kLargestEncodedRange, work));
    const auto reads = large.Reads({0});
    Check(reads.size() == std::size_t(kLargestEncodedRange) + 2);
    for (std::size_t i = 0; i < reads.size(); ++i)
      Check(reads[i].ordinal == i);
    Check(reads[1].value == reads[2].value);
    Check(std::get<abc::RegisterId>(large.Values()[reads.back().value.value].slot)
              .value == kLargestEncodedRange - 1U);
  }
  const auto hole = build(Body({0x70, 0xfe, 9, 0, 0, 0x64}));
  Check(hole.Reads({1})[0].value == *hole.Write({0}));
  Check(hole.Reads({2})[0].value == *hole.Write({0}));
  Check(!hole.Write({1}));
  const auto legacy = build(Body({0xfc, 0x20, 0, 2, 0x64}));
  Check(std::get<abc::RegisterId>(
            legacy.Values()[legacy.Reads({0})[0].value.value].slot)
            .value == 2);
  Check(!legacy.Write({0}));
  const auto dead = build(Body({0x65, 0x26, 0, 0, 0x65}));
  Check(dead.Values().empty() && dead.Reads({1}).empty() &&
        dead.work.rename_instructions == 1);
  const auto sparse = build(body, UINT64_MAX);
  Check(sparse.AllocatedBytes() == ssa.AllocatedBytes() &&
        sparse.work.peak_bytes == ssa.work.peak_bytes);
  const auto rejected = [](const abc::MethodBody& input, NormalFlow flow) {
    IdfQuota work{UINT64_MAX};
    return std::get<RegisterUseFailure>(
        RegisterSsa::Build(input, flow, 2, work));
  };
  auto malformed = body;
  malformed.tries.push_back({});
  Check(rejected(malformed, graph).kind == RegisterUseError::kExceptionRegions);
  auto resume = graph;
  resume.blocks[0].successors[0].kind = FlowEdgeKind::kResume;
  Check(rejected(body, resume).kind == RegisterUseError::kResumeEdge);
  const auto unknown = Body({0x26, 0, 0, 0x65});
  Check(rejected(unknown, std::get<NormalFlow>(BuildNormalFlow(unknown)))
            .instruction == ScalarError::kUnsupportedOpcode);
  Check(rejected(body, {}).kind == RegisterUseError::kInvalidGraph);
  IdfQuota exact{ssa.work.units};
  Check(std::get<RegisterSsa>(
            RegisterSsa::Build(body, graph, 2, exact, {ssa.work.peak_bytes}))
            .AllocatedBytes() == ssa.AllocatedBytes());
  Check(exact.Remaining() == 0);
  IdfQuota short_work{ssa.work.units - 1};
  Check(std::get<SsaError>(RegisterSsa::Build(body, graph, 2, short_work)) ==
        SsaError::kWorkLimit);
  Check(short_work.Remaining() == 0);
  IdfQuota short_bytes{UINT64_MAX};
  Check(std::get<SsaError>(RegisterSsa::Build(body, graph, 2, short_bytes,
                                              {ssa.work.peak_bytes - 1})) ==
        SsaError::kResourceLimit);
  IdfQuota exhausted{0};
  Check(std::get<RegisterUseFailure>(
            RegisterSsa::Build(body, graph, 2, exhausted))
            .kind == RegisterUseError::kWorkLimit);
  // Exercise failures in extraction, live-in/IDF, renaming and publication.
  for (std::uint64_t limit = 0; limit < ssa.work.units; ++limit) {
    IdfQuota limited{limit};
    const auto result = RegisterSsa::Build(body, graph, 2, limited);
    if (const auto* error = std::get_if<RegisterUseFailure>(&result))
      Check(error->kind == RegisterUseError::kWorkLimit);
    else if (const auto* error = std::get_if<IdfError>(&result))
      Check(*error == IdfError::kWorkLimit);
    else
      Check(std::get<SsaError>(result) == SsaError::kWorkLimit);
    Check(limited.Remaining() == 0);
  }
  constexpr std::size_t kMemorySweepStep = 37;
  for (std::size_t limit = 0; limit < ssa.work.peak_bytes;
       limit += kMemorySweepStep) {
    IdfQuota unlimited{UINT64_MAX};
    const auto result = RegisterSsa::Build(body, graph, 2, unlimited, {limit});
    if (const auto* error = std::get_if<RegisterUseFailure>(&result))
      Check(error->kind == RegisterUseError::kResourceLimit);
    else if (const auto* error = std::get_if<IdfError>(&result))
      Check(*error == IdfError::kResourceLimit);
    else if (const auto* error = std::get_if<DominatorError>(&result))
      Check(*error == DominatorError::kResourceLimit);
    else
      Check(std::get<SsaError>(result) == SsaError::kResourceLimit);
  }
  for (const std::size_t count : {200, 201, 4096, 65536}) {
    std::vector<std::uint8_t> code;
    for (std::size_t i = 0; i < count; ++i) {
      const auto low = static_cast<std::uint8_t>(i & UINT8_MAX);
      const auto high = static_cast<std::uint8_t>(i >> 8);
      code.insert(code.end(), {0x8f, low, high, low, high});
    }
    code.push_back(0x65);
    const auto many_slots = build(Body(code), UINT64_MAX);
    Check(many_slots.work.phis == 0 &&
          many_slots.work.rename_instructions == count + 1);
    Check(many_slots.Values().size() == 2 * count);
    Check(many_slots.work.units < 500 * count);
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto id = many_slots.Reads({i})[0].value;
      Check(std::holds_alternative<MethodEntry>(
          many_slots.Values()[id.value].origin));
      Check(
          std::get<abc::RegisterId>(many_slots.Values()[id.value].slot).value ==
          i);
    }
    code.clear();
    for (std::size_t i = 0; i < count; ++i)
      code.insert(code.end(), {0x45, 0, 0, 0x4d, 2});
    code.push_back(0x65);
    const auto deep = build(Body(code), 1);
    Check(deep.work.phis == 0 &&
          deep.work.rename_instructions == 2 * count + 1);
    Check(deep.Values().size() == count + 1);
    Check(deep.work.units < 500 * count);
    for (std::uint32_t i = 1; i < count; ++i)
      Check(deep.Reads({2 * i})[0].value == *deep.Write({2 * (i - 1)}));
  }
  // Many independent upward uses on a chain repeat live-in work. The method
  // quota MUST survive each variable query; this is not an asymptotic fix.
  std::vector<std::uint8_t> repeated;
  constexpr std::size_t kRepeatedBindings = 1024;
  for (std::size_t i = 0; i < kRepeatedBindings; ++i) {
    const auto low = static_cast<std::uint8_t>(i & UINT8_MAX);
    const auto high = static_cast<std::uint8_t>(i >> 8);
    repeated.insert(repeated.end(), {0x8f, low, high, low, high, 0x4d, 2});
  }
  repeated.push_back(0x65);
  const auto adversarial = Body(repeated);
  const auto adversarial_graph =
      std::get<NormalFlow>(BuildNormalFlow(adversarial));
  IdfQuota method_quota{1000000};
  Check(std::get<IdfError>(RegisterSsa::Build(
            adversarial, adversarial_graph, kRepeatedBindings, method_quota)) ==
        IdfError::kWorkLimit);
  Check(method_quota.Remaining() == 0);
  VerifySsaPaths();
}
