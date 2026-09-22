#include "abcd/analysis/own_receiver.h"

#include <cstdlib>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
abcd::abc::MethodBody Body(std::initializer_list<std::uint8_t> bytes) {
  abcd::abc::MethodBody body;
  const std::span input(bytes.begin(), bytes.size());
  for (std::size_t at = 0; at < input.size();) {
    const auto instruction =
        std::get<abcd::abc::DecodedInstruction>(abcd::abc::DecodeInstruction(
            input.subspan(at), abcd::abc::ProfileId::kDynamic1206));
    body.instructions.push_back(
        {{static_cast<std::uint32_t>(at)}, instruction, {}});
    if (instruction.branch_delta)
      body.instructions.back().branch_target = abcd::abc::CodeOffset{
          static_cast<std::uint32_t>(at + *instruction.branch_delta)};
    at += instruction.size;
  }
  return body;
}
}  // namespace
int main() {
  using namespace abcd;
  using namespace analysis;
  // Load entry this (r2), copy to r0, define an own field, overwrite r0.
  auto body = Body({0x60, 2, 0x61, 0, 0xdb, 0, 0, 0, 0, 0x00, 0x61, 0, 0x7a, 0,
                    0, 0, 0, 0x65});
  auto result = std::get<std::vector<OwnReceiver>>(
      ProveOwnReceivers(body, 3, abc::RegisterId{2}));
  Check(result.size() == 2);
  Check(result[0].origin == ReceiverOrigin::kEntryThis);
  Check(result[1].origin == ReceiverOrigin::kUnknown);
  Check(result[0].pc.value == 4);
  // A backedge can re-enter the prefix with different register values.
  const auto loop = Body({0x60, 2, 0x61, 0, 0xdb, 0, 0, 0, 0, 0x4d, 0xf7});
  result = std::get<std::vector<OwnReceiver>>(
      ProveOwnReceivers(loop, 3, abc::RegisterId{2}));
  Check(result[0].origin == ReceiverOrigin::kUnknown);
  body.handlers.push_back({{}, {4}, 1});
  result = std::get<std::vector<OwnReceiver>>(
      ProveOwnReceivers(body, 3, abc::RegisterId{2}));
  Check(result[0].origin == ReceiverOrigin::kUnknown);
  Check(std::get<ReceiverError>(ProveOwnReceivers(
            body, 3, abc::RegisterId{3})) == ReceiverError::kFrameRange);
  Check(std::get<ReceiverError>(
            ProveOwnReceivers(body, 3, abc::RegisterId{2}, {0})) ==
        ReceiverError::kResourceLimit);
  auto unknown = Body({0x60, 2, 0x61, 0, 0xd5, 0xdb, 0, 0, 0, 0, 0x65});
  unknown.instructions[2].decoded.opcode = abc::Opcode::kLDTHIS;
  result = std::get<std::vector<OwnReceiver>>(
      ProveOwnReceivers(unknown, 3, abc::RegisterId{2}));
  Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kUnknown);
  const auto direct = Body({0x7a, 0, 0, 0, 0, 0x65});
  for (const std::uint32_t registers : {200, 201, 400, 800, 1600, 40000}) {
    result = std::get<std::vector<OwnReceiver>>(
        ProveOwnReceivers(direct, registers, abc::RegisterId{0}));
    Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kEntryThis);
    result = std::get<std::vector<OwnReceiver>>(
        ProveOwnReceivers(direct, registers, {}));
    Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kUnknown);
  }
  const auto moved = Body({0x45, 0, 2, 0x45, 0, 0, 0x7a, 0, 0, 0, 0, 0x65});
  result = std::get<std::vector<OwnReceiver>>(
      ProveOwnReceivers(moved, 3, abc::RegisterId{2}));
  Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kEntryThis);
  const auto branched = Body({0x4d, 2, 0x7a, 0, 0, 0, 0, 0x65});
  result = std::get<std::vector<OwnReceiver>>(
      ProveOwnReceivers(branched, 3, abc::RegisterId{0}));
  Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kUnknown);
  const auto called = Body({0x45, 0, 2, 0x29, 0, 0x00, 0x61, 2, 0x7a, 0, 0, 0,
                            0, 0x7a, 0, 0, 0, 2, 0x65});
  result = std::get<std::vector<OwnReceiver>>(
      ProveOwnReceivers(called, 3, abc::RegisterId{2}));
  Check(result.size() == 2 && result[0].origin == ReceiverOrigin::kEntryThis &&
        result[1].origin == ReceiverOrigin::kUnknown);
  constexpr std::size_t kExactBudget = 3 + sizeof(OwnReceiver);
  Check(std::holds_alternative<std::vector<OwnReceiver>>(
      ProveOwnReceivers(direct, 3, abc::RegisterId{0}, {kExactBudget})));
  Check(std::get<ReceiverError>(ProveOwnReceivers(direct, 3, abc::RegisterId{0},
                                                  {kExactBudget - 1})) ==
        ReceiverError::kResourceLimit);
}
