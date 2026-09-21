#include "abcd/analysis/accumulator_flow.h"

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
    auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(
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
             analysis::AccumulatorBudget budget = {}) {
  const auto graph =
      std::get<analysis::NormalFlow>(analysis::BuildNormalFlow(body));
  return analysis::AnalyzeAcyclicAccumulator(body, graph, 1, budget);
}
}  // namespace
int main() {
  using namespace analysis;
  const std::uint8_t diamond[]{0x02, 0x4f, 5, 0x00, 0x4d, 3, 0x01, 0x64};
  auto body = Body(diamond);
  const auto checked = std::get<AccumulatorFlow>(Analyze(body));
  Check(checked.entries.front() == AccumulatorState::kMayBeHole);
  Check(checked.entries.back() == AccumulatorState::kValue);
  Check(checked.instruction_visits == body.instructions.size());
  Check(std::holds_alternative<AccumulatorFlow>(
      Analyze(body, {checked.allocated_bytes})));
  Check(
      std::get<AccumulatorFailure>(Analyze(body, {checked.allocated_bytes - 1}))
          .kind == AccumulatorError::kOrder);
  auto hole = std::vector<std::uint8_t>(std::begin(diamond), std::end(diamond));
  hole[3] = 0x70;
  const auto failure = std::get<AccumulatorFailure>(Analyze(Body(hole)));
  Check(failure.kind == AccumulatorError::kReadBeforeValue &&
        failure.offset.value == 7);
  const auto hole_body = Body(hole);
  auto reversed = std::get<NormalFlow>(BuildNormalFlow(hole_body));
  std::swap(reversed.blocks[0].successors[0], reversed.blocks[0].successors[1]);
  Check(std::get<AccumulatorFailure>(
            AnalyzeAcyclicAccumulator(hole_body, reversed, 1))
            .kind == AccumulatorError::kReadBeforeValue);
  const std::uint8_t unread[]{0x4d, 3, 0x00, 0x64};
  Check(std::get<AccumulatorFailure>(Analyze(Body(unread))).kind ==
        AccumulatorError::kReadBeforeValue);
  const std::uint8_t safe_dead[]{0x00, 0x4d, 3, 0x70, 0x64};
  const auto dead = std::get<AccumulatorFlow>(Analyze(Body(safe_dead)));
  Check(dead.order.unreachable == 1 &&
        dead.entries[1] == AccumulatorState::kUnreachable);
  const std::uint8_t restore[]{0x70, 0x60, 0, 0x64};
  Check(std::holds_alternative<AccumulatorFlow>(Analyze(Body(restore))));
  const std::uint8_t loop[]{0x00, 0x4d, 0xff};
  Check(std::get<AccumulatorFailure>(Analyze(Body(loop))).order_error ==
        OrderError::kCycle);
  const std::uint8_t suspend[]{0xc3, 0, 0x65};
  Check(std::get<AccumulatorFailure>(Analyze(Body(suspend))).order_error ==
        OrderError::kResumeEdge);
  const std::uint8_t unsupported[]{0x00, 0xfd, 8, 0, 0, 0, 0, 0x64};
  const auto rejected =
      std::get<AccumulatorFailure>(Analyze(Body(unsupported)));
  Check(rejected.kind == AccumulatorError::kUnsupportedInstruction &&
        rejected.offset.value == 1);
  auto malformed = std::get<NormalFlow>(BuildNormalFlow(body));
  malformed.blocks[0].first_instruction = 1;
  Check(std::get<AccumulatorFailure>(
            AnalyzeAcyclicAccumulator(body, malformed, 1))
            .kind == AccumulatorError::kInvalidGraph);
  Check(std::get<AccumulatorFailure>(Analyze(body, {0})).kind ==
        AccumulatorError::kResourceLimit);
  body.tries.push_back({});
  Check(std::get<AccumulatorFailure>(Analyze(body)).kind ==
        AccumulatorError::kExceptionRegions);
  for (const std::uint32_t count : {200U, 201U, 4096U, 65536U}) {
    std::vector<std::uint8_t> bytes{0x00};
    for (std::uint32_t i = 0; i < count; ++i)
      bytes.insert(bytes.end(), {0x4f, 2});
    bytes.push_back(0x64);
    const auto chain = Body(bytes);
    const auto result = std::get<AccumulatorFlow>(Analyze(chain));
    Check(result.instruction_visits == count + 2);
    Check(result.entries.back() == AccumulatorState::kValue);
  }
}
