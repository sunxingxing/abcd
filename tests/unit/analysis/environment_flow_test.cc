#include "abcd/analysis/environment_flow.h"

#include <cstdlib>
#include <utility>

namespace {
using namespace abcd;
void Check(bool value) {
  if (!value) std::abort();
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
          static_cast<std::uint32_t>(std::int64_t(pc) + *decoded.branch_delta)};
    body.instructions.push_back(instruction);
    pc += decoded.size;
  }
  return body;
}
auto Analyze(const abc::MethodBody& body,
             analysis::EnvironmentBudget budget = {}) {
  const auto graph =
      std::get<analysis::NormalFlow>(analysis::BuildNormalFlow(body));
  return analysis::AnalyzeEnvironments(body, graph, 1, budget);
}
}  // namespace

int main() {
  using namespace analysis;
  const EnvironmentRef entry = CapturedDepth{0};
  const EnvironmentRef local = EnvironmentId{0};
  const std::uint8_t linear[]{0x09, 2,    0x3c, 0, 0x3d,
                              0x10, 0x69, 0x3c, 0, 0x65};
  auto body = Body(linear);
  const auto flow = std::get<EnvironmentFlow>(Analyze(body));
  Check(flow.environments.Size() == 1 && flow.allocations.size() == 1);
  Check(flow.before.size() == body.instructions.size());
  Check(flow.before[0] == entry && flow.before[1] == local &&
        flow.before[4] == entry);
  Check(flow.bindings.size() == 3);
  Check(flow.bindings[0].binding.environment == local &&
        flow.bindings[0].binding.slot.value == 0);
  Check(flow.bindings[1].binding.environment == local &&
        flow.bindings[1].binding.slot.value == 1);
  Check(flow.bindings[2].binding.environment == entry);
  Check(flow.bindings[0].instruction == 1 &&
        flow.allocations[0].instruction == 0);
  Check(std::holds_alternative<EnvironmentFlow>(
      Analyze(body, {flow.allocated_bytes})));
  Check(std::holds_alternative<EnvironmentFlowFailure>(
      Analyze(body, {flow.allocated_bytes - 1})));
  Check(std::holds_alternative<EnvironmentFlowFailure>(Analyze(body, {0})));

  const std::uint8_t creation[]{0x09, 1,    0x33, 0, 0, 0, 0,
                                0x69, 0x33, 0,    0, 0, 0, 0x65};
  const auto captured = std::get<EnvironmentFlow>(Analyze(Body(creation)));
  Check(captured.before[1] == local && captured.before[3] == entry);

  const std::uint8_t diamond[]{0x09, 1,    0x02, 0x4f, 7,    0x09, 1,
                               0x69, 0x4d, 3,    0xd5, 0x3c, 0,    0x65};
  const auto joined = std::get<EnvironmentFlow>(Analyze(Body(diamond)));
  Check(joined.environments.Size() == 2 && joined.bindings.size() == 1);
  Check(joined.bindings[0].binding.environment == local);
  auto unequal =
      std::vector<std::uint8_t>(std::begin(diamond), std::end(diamond));
  unequal[7] = 0xd5;
  const auto conflict =
      std::get<EnvironmentFlowFailure>(Analyze(Body(unequal)));
  Check(conflict.kind == EnvironmentFlowError::kAmbiguousJoin &&
        conflict.offset.value == 11);

  const std::uint8_t backward[]{0x4d, 6,    0x3c, 0,    0x69,
                                0x65, 0x09, 1,    0x4d, 0xfa};
  const auto reordered = std::get<EnvironmentFlow>(Analyze(Body(backward)));
  Check(reordered.bindings[0].instruction == 1 &&
        reordered.bindings[0].binding.environment == local);
  Check(reordered.allocations[0].instruction == 4);
  const std::uint8_t skipped[]{0x4d, 6, 0x09, 0, 0x3c, 0, 0x65};
  const auto dead = std::get<EnvironmentFlow>(Analyze(Body(skipped)));
  Check(!dead.before[1] && !dead.before[2] && dead.bindings.empty() &&
        dead.environments.Size() == 0);
  const std::uint8_t bounds[]{0x09, 0, 0x3c, 0, 0x65};
  const auto invalid = std::get<EnvironmentFlowFailure>(Analyze(Body(bounds)));
  Check(invalid.kind == EnvironmentFlowError::kBinding &&
        invalid.offset.value == 2 &&
        invalid.environment_error == EnvironmentError::kSlotRange);
  const std::uint8_t loop[]{0x4d, 0};
  Check(std::holds_alternative<EnvironmentFlow>(Analyze(Body(loop))));
  const std::uint8_t loop_scope[]{0x09, 1, 0x3c, 0, 0x69, 0x4d, 0xfb};
  const auto repeated = std::get<EnvironmentFlow>(Analyze(Body(loop_scope)));
  Check(repeated.environments.Size() == 1 && repeated.bindings.size() == 1);
  Check(repeated.bindings[0].binding.environment == local);
  const std::uint8_t growing[]{0x09, 1, 0x4d, 0xfe};
  Check(std::get<EnvironmentFlowFailure>(Analyze(Body(growing))).kind ==
        EnvironmentFlowError::kAmbiguousJoin);
  const std::uint8_t resume[]{0xc3, 0, 0x65};
  Check(std::get<EnvironmentFlowFailure>(Analyze(Body(resume))).kind ==
        EnvironmentFlowError::kResumeEdge);
  auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  graph.blocks[0].first_instruction = 1;
  Check(std::get<EnvironmentFlowFailure>(AnalyzeEnvironments(body, graph, 1))
            .kind == EnvironmentFlowError::kInvalidGraph);
  body.tries.push_back({});
  Check(std::get<EnvironmentFlowFailure>(Analyze(body)).kind ==
        EnvironmentFlowError::kExceptionRegions);

  for (const std::uint32_t count : {200U, 201U, 4096U, 65536U}) {
    std::vector<std::uint8_t> bytes;
    for (std::uint32_t i = 0; i < count; ++i)
      bytes.insert(bytes.end(), {0x09, 1});
    const auto depth = count - 1;
    for (std::uint32_t i = 0; i < count; ++i)
      bytes.insert(bytes.end(), {0xfd, 0x0c, static_cast<std::uint8_t>(depth),
                                 static_cast<std::uint8_t>(depth >> 8), 0, 0});
    bytes.push_back(0x65);
    const auto large_body = Body(bytes);
    const auto large = std::get<EnvironmentFlow>(Analyze(large_body));
    Check(large.environments.Size() == count && large.bindings.size() == count);
    Check(large.instruction_visits <= 3 * large_body.instructions.size());
    for (const auto& use : large.bindings)
      Check(use.binding.environment == local);
  }
}
