#include "abcd/analysis/derived_constructor.h"

#include <array>
#include <cstdlib>
#include <vector>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
abcd::abc::MethodBody Body(std::span<const std::uint8_t> bytes) {
  abcd::abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abcd::abc::DecodedInstruction>(
        abcd::abc::DecodeInstruction(bytes.subspan(pc),
                                     abcd::abc::ProfileId::kDynamic1301));
    body.instructions.push_back({{pc}, decoded, {}});
    pc += decoded.size;
  }
  return body;
}
}  // namespace

int main() {
  using namespace abcd;
  constexpr std::uint8_t kMove = 0x44;
  constexpr std::uint8_t kLoad = 0x60;
  constexpr std::uint8_t kStore = 0x61;
  constexpr std::uint8_t kReturn = 0x64;
  constexpr std::uint8_t kNop = 0xd5;
  constexpr std::uint8_t kRuntimePrefix = 0xfb;
  constexpr std::uint8_t kSuperForward = 0x19;
  constexpr std::uint8_t kCheckPrefix = 0xfe;
  constexpr std::uint8_t kSuperCheck = 7;
  constexpr std::uint8_t kRequireUninitialized = 1;
  constexpr std::uint8_t kRequireInitialized = 0;
  const std::array<std::uint8_t, 3> forward_bytes{
      kRuntimePrefix, kSuperForward, 5};
  const auto forward = std::get<abc::DecodedInstruction>(
      abc::DecodeInstruction(forward_bytes, abc::ProfileId::kDynamic1301));
  const auto lifted = std::get<analysis::SuperForward>(
      analysis::LiftSuperInstruction(forward, 10));
  Check(lifted.constructor.value == 5);
  Check(std::get<analysis::SuperError>(
            analysis::LiftSuperInstruction(forward, 5)) ==
        analysis::SuperError::kRegisterRange);
  for (const auto [raw, kind] : {
           std::pair{kRequireUninitialized,
                     analysis::SuperCheckKind::kRequireUninitialized},
           std::pair{kRequireInitialized,
                     analysis::SuperCheckKind::kRequireInitialized},
           std::pair{std::uint8_t{2}, analysis::SuperCheckKind::kNoop}}) {
    const std::array<std::uint8_t, 3> check_bytes{
        kCheckPrefix, kSuperCheck, raw};
    const auto decoded = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(check_bytes, abc::ProfileId::kDynamic1301));
    Check(std::get<analysis::SuperCheck>(
              analysis::LiftSuperInstruction(decoded, 10))
              .kind == kind);
  }
  auto malformed = forward;
  malformed.operand_count = 0;
  Check(std::get<analysis::SuperError>(
            analysis::LiftSuperInstruction(malformed, 10)) ==
        analysis::SuperError::kMalformedInstruction);

  // The v7 EntryAbility constructor copies ABI slots, forwards every actual
  // argument, binds the returned object as this, then returns that object.
  const std::vector<std::uint8_t> instructions{
      kMove, 0x60, kMove, 0x71, kMove, 0x82, kMove, 0x93,
      kLoad, 0, kStore, 5,
      kRuntimePrefix, kSuperForward, 5, kStore, 5,
      kLoad, 2, kCheckPrefix, kSuperCheck, kRequireUninitialized,
      kLoad, 5, kStore, 2, kLoad, 2,
      kCheckPrefix, kSuperCheck, kRequireInitialized, kReturn};
  abc::FrameLayout frame;
  frame.locals = 6;
  frame.function = abc::RegisterId{6};
  frame.new_target = abc::RegisterId{7};
  frame.this_value = abc::RegisterId{8};
  frame.first_parameter = {9};
  frame.parameters = 1;
  frame.registers = 10;
  const auto prove = [&](std::span<const std::uint8_t> code,
                         analysis::DerivedConstructorBudget budget = {}) {
    return analysis::ProveForwardingDerivedConstructor(Body(code), frame,
                                                       budget);
  };
  Check(std::holds_alternative<analysis::ForwardingDerivedConstructor>(
      prove(instructions)));
  auto bad_frame = frame;
  bad_frame.this_value.reset();
  Check(std::get<analysis::DerivedConstructorError>(
            analysis::ProveForwardingDerivedConstructor(
                Body(instructions), bad_frame)) ==
        analysis::DerivedConstructorError::kFrame);
  auto branched = Body(instructions);
  branched.instructions[6].branch_target = abc::CodeOffset{0};
  Check(std::get<analysis::DerivedConstructorError>(
            analysis::ProveForwardingDerivedConstructor(branched, frame)) ==
        analysis::DerivedConstructorError::kControlFlow);
  auto wrong = instructions;
  wrong[14] = 3;
  Check(std::get<analysis::DerivedConstructorError>(prove(wrong)) ==
        analysis::DerivedConstructorError::kValue);
  wrong = instructions;
  wrong[21] = kRequireInitialized;
  Check(std::get<analysis::DerivedConstructorError>(prove(wrong)) ==
        analysis::DerivedConstructorError::kGuard);
  wrong = instructions;
  wrong.insert(wrong.end() - 1, {0x62, 0, 0, 0, 0});
  Check(std::get<analysis::DerivedConstructorError>(prove(wrong)) ==
        analysis::DerivedConstructorError::kInstruction);
  analysis::DerivedConstructorBudget denied;
  denied.bytes = 0;
  Check(std::get<analysis::DerivedConstructorError>(
            prove(instructions, denied)) ==
        analysis::DerivedConstructorError::kResourceLimit);
  for (const std::size_t count : {200U, 201U}) {
    auto longer = instructions;
    longer.insert(longer.begin() + 8, count, kNop);
    Check(std::holds_alternative<analysis::ForwardingDerivedConstructor>(
        prove(longer)));
  }
}
