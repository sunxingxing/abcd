#include "abcd/analysis/normal_flow.h"

#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace {
using namespace abcd::abc;
using namespace abcd::analysis;

void Check(bool condition) {
  if (!condition) {
    std::cerr << "normal_flow_test failed\n";
    std::abort();
  }
}

MethodBody Body(std::span<const std::uint8_t> bytes) {
  MethodBody body;
  std::uint32_t pc = 0;
  while (pc < bytes.size()) {
    auto result = DecodeInstruction(bytes.subspan(pc), ProfileId::kDynamic1301);
    Check(std::holds_alternative<DecodedInstruction>(result));
    auto instruction = std::get<DecodedInstruction>(result);
    BodyInstruction located{CodeOffset{pc}, instruction, {}};
    if (instruction.branch_delta)
      located.branch_target = CodeOffset{static_cast<std::uint32_t>(
          static_cast<std::int64_t>(pc) + *instruction.branch_delta)};
    body.instructions.push_back(located);
    pc += instruction.size;
  }
  return body;
}

void Edge(const FlowBlock& block, std::size_t index, std::uint32_t target,
          FlowEdgeKind kind) {
  Check(index < block.successor_count);
  Check(block.successors[index].target == BlockId{target});
  Check(block.successors[index].kind == kind);
}

void Fails(const MethodBody& body, FlowError error, FlowBudget budget = {}) {
  const auto result = BuildNormalFlow(body, budget);
  Check(std::holds_alternative<FlowFailure>(result));
  Check(std::get<FlowFailure>(result).kind == error);
}

void DiamondAndUnreachableCode() {
  // jeqz +5; ldundefined; jmp +3; ldnull; returnundefined; unreachable return.
  const std::uint8_t bytes[]{0x4f, 5, 0x00, 0x4d, 3, 0x01, 0x65, 0x64};
  const auto body = Body(bytes);
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  Check(graph.blocks.size() == 5);
  Check(graph.blocks[0].begin == CodeOffset{0});
  Check(graph.blocks[0].end == CodeOffset{2});
  Edge(graph.blocks[0], 0, 2, FlowEdgeKind::kTaken);
  Edge(graph.blocks[0], 1, 1, FlowEdgeKind::kFallthrough);
  Edge(graph.blocks[1], 0, 3, FlowEdgeKind::kTaken);
  Edge(graph.blocks[2], 0, 3, FlowEdgeKind::kFallthrough);
  Check(graph.blocks[3].successor_count == 0);
  Check(graph.blocks[4].successor_count == 0);
  Check(graph.blocks[1].first_instruction == 1);
  Check(graph.blocks[1].instruction_count == 2);

  const std::uint8_t loop[]{0x00, 0x4d, 0xff};
  const auto cycle = std::get<NormalFlow>(BuildNormalFlow(Body(loop)));
  Check(cycle.blocks.size() == 1);
  Edge(cycle.blocks[0], 0, 0, FlowEdgeKind::kTaken);
  const std::uint8_t shared[]{0x4f, 2, 0x65};
  const auto alternatives = std::get<NormalFlow>(BuildNormalFlow(Body(shared)));
  Check(alternatives.blocks[0].successor_count == 2);
  Edge(alternatives.blocks[0], 0, 1, FlowEdgeKind::kTaken);
  Edge(alternatives.blocks[0], 1, 1, FlowEdgeKind::kFallthrough);
}

void SuspensionAndThrow() {
  const std::uint8_t suspend[]{0xc3, 0, 0x65};
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(Body(suspend)));
  Check(graph.blocks.size() == 2);
  Check(graph.blocks[0].transfer == ControlTransfer::kSuspend);
  Edge(graph.blocks[0], 0, 1, FlowEdgeKind::kResume);
  const std::uint8_t throws[]{0xfe, 0x00, 0x65};
  const auto thrown = std::get<NormalFlow>(BuildNormalFlow(Body(throws)));
  Check(thrown.blocks.size() == 2);
  Check(thrown.blocks[0].transfer == ControlTransfer::kThrow);
  Check(thrown.blocks[0].successor_count == 0);
  const std::uint8_t guard[]{0xfe, 0x05, 0, 0x65};
  const auto guarded = std::get<NormalFlow>(BuildNormalFlow(Body(guard)));
  Check(guarded.blocks.size() == 1);
  Check(guarded.blocks[0].instruction_count == 2);
}

void ExceptionBoundaries() {
  const std::uint8_t bytes[]{0x00, 0x00, 0x00, 0x00, 0x65};
  auto body = Body(bytes);
  body.tries = {{{0}, 3, 0, 1}, {{1}, 3, 1, 1}};
  body.handlers = {{{}, {3}, 2}, {{}, {4}, 1}};
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  Check(graph.blocks.size() == 4);
  Check(graph.blocks[0].end == CodeOffset{1});
  Check(graph.blocks[1].end == CodeOffset{3});
  Check(graph.blocks[2].end == CodeOffset{4});
  Check(graph.blocks[3].end == CodeOffset{5});
  body.tries[0].length = 100;
  Fails(body, FlowError::kInvalidTarget);
  body.tries[0].length = 3;
  body.tries[0].handler_count = 3;
  Fails(body, FlowError::kMalformedBody);
}

void InvalidBodiesAndBudgets() {
  Fails({}, FlowError::kEmptyBody);
  const std::uint8_t nop[]{0xd5};
  Fails(Body(nop), FlowError::kFallthroughPastEnd);
  const std::uint8_t interior[]{0x4d, 1, 0x65};
  Fails(Body(interior), FlowError::kInvalidTarget);
  const std::uint8_t branch[]{0x4f, 0};
  Fails(Body(branch), FlowError::kFallthroughPastEnd);
  const std::uint8_t valid[]{0x00, 0x65};
  auto body = Body(valid);
  body.instructions[1].offset.value = 0;
  Fails(body, FlowError::kMalformedBody);
  body = Body(valid);
  body.instructions[0].branch_target = CodeOffset{1};
  Fails(body, FlowError::kMalformedBody);
  body = Body(valid);
  body.instructions[0].decoded.opcode = static_cast<Opcode>(UINT16_MAX);
  Fails(body, FlowError::kMalformedBody);
  Fails(Body(valid), FlowError::kResourceLimit, {1});
}

void Scaling() {
  for (const auto count : {200U, 201U, 1024U, 8192U}) {
    std::vector<std::uint8_t> bytes;
    for (std::uint32_t i = 0; i < count; ++i)
      bytes.insert(bytes.end(), {0x4f, 2});
    bytes.push_back(0x65);
    auto body = Body(bytes);
    const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
    Check(graph.blocks.size() == count + 1);
    for (std::uint32_t i = 0; i < count; ++i) {
      Edge(graph.blocks[i], 0, i + 1, FlowEdgeKind::kTaken);
      Edge(graph.blocks[i], 1, i + 1, FlowEdgeKind::kFallthrough);
    }
  }
}
}  // namespace

int main() {
  DiamondAndUnreachableCode();
  SuspensionAndThrow();
  ExceptionBoundaries();
  InvalidBodiesAndBudgets();
  Scaling();
}
