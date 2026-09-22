#include "abcd/analysis/property_receiver.h"

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
  auto result = std::get<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(body, 3, abc::RegisterId{2}));
  Check(result.size() == 2);
  Check(result[0].origin == ReceiverOrigin::kEntryThis);
  Check(result[1].origin == ReceiverOrigin::kUnknown);
  Check(result[0].pc.value == 4);
  // A backedge can re-enter the prefix with different register values.
  const auto loop = Body({0x60, 2, 0x61, 0, 0xdb, 0, 0, 0, 0, 0x4d, 0xf7});
  result = std::get<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(loop, 3, abc::RegisterId{2}));
  Check(result[0].origin == ReceiverOrigin::kUnknown);
  body.handlers.push_back({{}, {4}, 1});
  result = std::get<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(body, 3, abc::RegisterId{2}));
  Check(result[0].origin == ReceiverOrigin::kUnknown);
  Check(std::get<ReceiverError>(ProvePropertyReceivers(
            body, 3, abc::RegisterId{3})) == ReceiverError::kFrameRange);
  Check(std::get<ReceiverError>(
            ProvePropertyReceivers(body, 3, abc::RegisterId{2}, {0})) ==
        ReceiverError::kResourceLimit);
  auto unknown = Body({0x60, 2, 0x61, 0, 0xd5, 0xdb, 0, 0, 0, 0, 0x65});
  unknown.instructions[2].decoded.opcode = abc::Opcode::kLDNEWTARGET;
  result = std::get<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(unknown, 3, abc::RegisterId{2}));
  Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kUnknown);
  // LDTHIS reads frame.thisObj, not the optional ABI register copy.
  constexpr std::uint8_t kLoadThis = 0x6f;
  const auto frame_this =
      Body({0x00, 0x61, 2, kLoadThis, 0x61, 0, 0xdb, 0, 0, 0, 0, 0x65});
  for (const auto slot : {std::optional<abc::RegisterId>{},
                          std::optional<abc::RegisterId>{{2}}}) {
    result = std::get<std::vector<PropertyReceiver>>(
        ProvePropertyReceivers(frame_this, 3, slot));
    Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kEntryThis);
  }
  const auto direct = Body({0x7a, 0, 0, 0, 0, 0x65});
  const auto module_load = Body(
      {0x45, 0, 2, 0x7e, 0, 0x61, 1, 0x43, 0, 0, 0, 0, 0x43, 0, 0, 0, 1, 0x65});
  result = std::get<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(module_load, 3, abc::RegisterId{2}));
  Check(result.size() == 2 && result[0].origin == ReceiverOrigin::kEntryThis &&
        result[1].origin == ReceiverOrigin::kUnknown);
  const auto assigned = Body({0x45, 0, 2, 0x43, 0, 0, 0, 0, 0x65});
  result = std::get<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(assigned, 3, abc::RegisterId{2}));
  Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kEntryThis);
  // Receiver calls preserve the caller's register aliases, not accumulator.
  const auto receiver_call =
      Body({0x45, 0, 2, 0x2e, 0,    0, 2, 0x61, 1, 0xdb,
            0,    0, 0, 0,    0xdb, 0, 0, 0,    1, 0x65});
  result = std::get<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(receiver_call, 3, abc::RegisterId{2}));
  Check(result.size() == 2 && result[0].origin == ReceiverOrigin::kEntryThis &&
        result[1].origin == ReceiverOrigin::kUnknown);
  auto bad_call = receiver_call;
  bad_call.instructions[1].decoded.operands[1].raw_bits = 3;
  Check(std::get<ReceiverError>(ProvePropertyReceivers(
            bad_call, 3, abc::RegisterId{2})) == ReceiverError::kFrameRange);
  const auto global_write =
      Body({0x60, 2, 0x40, 0, 0, 0, 0x61, 0, 0xdb, 0, 0, 0, 0, 0x65});
  result = std::get<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(global_write, 3, abc::RegisterId{2}));
  Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kEntryThis);
  const auto global_read = Body({0x45, 0,    2, 0x3f, 0, 0, 0, 0xdb, 0,   0, 0,
                                 0,    0x61, 1, 0xdb, 0, 0, 0, 1,    0x65});
  result = std::get<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(global_read, 3, abc::RegisterId{2}));
  Check(result.size() == 2 && result[0].origin == ReceiverOrigin::kEntryThis &&
        result[1].origin == ReceiverOrigin::kUnknown);
  for (const std::uint32_t registers : {200, 201, 400, 800, 1600, 40000}) {
    result = std::get<std::vector<PropertyReceiver>>(
        ProvePropertyReceivers(direct, registers, abc::RegisterId{0}));
    Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kEntryThis);
    result = std::get<std::vector<PropertyReceiver>>(
        ProvePropertyReceivers(direct, registers, {}));
    Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kUnknown);
  }
  const auto moved = Body({0x45, 0, 2, 0x45, 0, 0, 0x7a, 0, 0, 0, 0, 0x65});
  result = std::get<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(moved, 3, abc::RegisterId{2}));
  Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kEntryThis);
  const auto branched = Body({0x4d, 2, 0x7a, 0, 0, 0, 0, 0x65});
  result = std::get<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(branched, 3, abc::RegisterId{0}));
  Check(result.size() == 1 && result[0].origin == ReceiverOrigin::kUnknown);
  const auto called = Body({0x45, 0, 2, 0x29, 0, 0x00, 0x61, 2, 0x7a, 0, 0, 0,
                            0, 0x7a, 0, 0, 0, 2, 0x65});
  result = std::get<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(called, 3, abc::RegisterId{2}));
  Check(result.size() == 2 && result[0].origin == ReceiverOrigin::kEntryThis &&
        result[1].origin == ReceiverOrigin::kUnknown);
  constexpr std::size_t kExactBudget = 3 + sizeof(PropertyReceiver);
  Check(std::holds_alternative<std::vector<PropertyReceiver>>(
      ProvePropertyReceivers(direct, 3, abc::RegisterId{0}, {kExactBudget})));
  Check(std::get<ReceiverError>(ProvePropertyReceivers(
            direct, 3, abc::RegisterId{0}, {kExactBudget - 1})) ==
        ReceiverError::kResourceLimit);
}
