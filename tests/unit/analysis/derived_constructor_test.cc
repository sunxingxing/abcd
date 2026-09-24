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
  const std::array<std::uint8_t, 4> positional_bytes{0x32, 7, 2, 3};
  const auto positional = std::get<abc::DecodedInstruction>(
      abc::DecodeInstruction(positional_bytes, abc::ProfileId::kDynamic1301));
  const auto range = std::get<analysis::SuperRange>(
      analysis::LiftSuperInstruction(positional, 11));
  Check(range.cache_slot == 7 && range.count == 2 &&
        range.first.value == 3);
  Check(std::get<analysis::SuperError>(
            analysis::LiftSuperInstruction(positional, 4)) ==
        analysis::SuperError::kRegisterRange);
  const std::array<std::uint8_t, 5> wide_bytes{0xfd, 6, 2, 0, 3};
  const auto wide = std::get<abc::DecodedInstruction>(
      abc::DecodeInstruction(wide_bytes, abc::ProfileId::kDynamic1301));
  const auto wide_range = std::get<analysis::SuperRange>(
      analysis::LiftSuperInstruction(wide, 11));
  Check(!wide_range.cache_slot && wide_range.count == 2 &&
        wide_range.first.value == 3);
  auto malformed_range = positional;
  malformed_range.operand_count = 2;
  Check(std::get<analysis::SuperError>(
            analysis::LiftSuperInstruction(malformed_range, 11)) ==
        analysis::SuperError::kMalformedInstruction);
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

  // This pure forwarding template excludes the real EntryAbility constructor,
  // which also uses COPYRESTARGS/SUPERCALLSPREAD and initializes a field.
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
  const std::vector<std::uint8_t> positional_instructions{
      kMove, 0x60, kMove, 0x71, kMove, 0x82, kMove, 0x93,
      kMove, 0xa4, kLoad, 0, kStore, 5,
      0x32, 7, 2, 3, kStore, 5,
      kLoad, 2, kCheckPrefix, kSuperCheck, kRequireUninitialized,
      kLoad, 5, kStore, 2, kLoad, 2,
      kCheckPrefix, kSuperCheck, kRequireInitialized, kReturn};
  auto positional_frame = frame;
  positional_frame.parameters = 2;
  positional_frame.registers = 11;
  const auto positional_proof =
      analysis::ProveForwardingDerivedConstructor(
          Body(positional_instructions), positional_frame);
  const auto* positional_result =
      std::get_if<analysis::ForwardingDerivedConstructor>(&positional_proof);
  Check(positional_result &&
        positional_result->kind == analysis::ForwardingKind::kParameters &&
        positional_result->argument_count == 2);
  auto wide_positional = positional_instructions;
  wide_positional.erase(wide_positional.begin() + 14,
                        wide_positional.begin() + 18);
  wide_positional.insert(wide_positional.begin() + 14,
                         wide_bytes.begin(), wide_bytes.end());
  Check(std::holds_alternative<analysis::ForwardingDerivedConstructor>(
      analysis::ProveForwardingDerivedConstructor(
          Body(wide_positional), positional_frame)));
  auto wrong_parameter = positional_instructions;
  wrong_parameter[9] = 0x94;
  Check(std::get<analysis::DerivedConstructorError>(
            analysis::ProveForwardingDerivedConstructor(
                Body(wrong_parameter), positional_frame)) ==
        analysis::DerivedConstructorError::kValue);
  for (const std::size_t count : {200U, 201U, 400U, 800U}) {
    auto longer = positional_instructions;
    longer.insert(longer.begin() + 10, count, kNop);
    Check(std::holds_alternative<analysis::ForwardingDerivedConstructor>(
        analysis::ProveForwardingDerivedConstructor(
            Body(longer), positional_frame)));
  }
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
