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
  const std::array<std::uint8_t, 3> spread_bytes{0xb9, 7, 5};
  const auto spread = std::get<abc::DecodedInstruction>(
      abc::DecodeInstruction(spread_bytes, abc::ProfileId::kDynamic1301));
  const auto spread_lifted = std::get<analysis::SuperSpread>(
      analysis::LiftSuperInstruction(spread, 11));
  Check(spread_lifted.arguments.value == 5 &&
        spread_lifted.cache_slot == 7);
  Check(std::get<analysis::SuperError>(
            analysis::LiftSuperInstruction(spread, 5)) ==
        analysis::SuperError::kRegisterRange);
  auto malformed_spread = spread;
  malformed_spread.operand_count = 1;
  Check(std::get<analysis::SuperError>(
            analysis::LiftSuperInstruction(malformed_spread, 11)) ==
        analysis::SuperError::kMalformedInstruction);
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
  auto rest_frame = frame;
  rest_frame.extra_arguments = true;
  const std::vector<std::uint8_t> field_constructor{
      kMove, 0x60, kMove, 0x71, kMove, 0x82, kMove, 0x93,
      0xcf, 0, kStore, 3, kLoad, 3, kStore, 5,
      kLoad, 0, 0xb9, 0, 5, kStore, 5,
      kLoad, 2, kCheckPrefix, kSuperCheck, kRequireUninitialized,
      kLoad, 5, kStore, 2, kLoad, 2,
      kCheckPrefix, kSuperCheck, kRequireInitialized,
      kStore, 5, 0x3e, 31, 0, 0x43, 2, 56, 0, 5,
      kLoad, 2, kCheckPrefix, kSuperCheck, kRequireInitialized,
      kReturn};
  const auto field_proof = analysis::ProveSpreadFieldDerivedConstructor(
      Body(field_constructor), rest_frame);
  // The shared sparse initialization proof admits real spread protocol and
  // arbitrary trailing effects; the old range API must retain its restriction.
  const auto spread_proof = analysis::ProveSpreadDerivedConstructor(
      Body(field_constructor), rest_frame);
  Check(std::holds_alternative<analysis::SpreadDerivedConstructor>(spread_proof));
  Check(std::get<analysis::SpreadDerivedConstructor>(spread_proof).super_pc.value == 18);
  Check(std::get<analysis::DerivedConstructorError>(
      analysis::ProveRangeDerivedConstructor(Body(field_constructor), rest_frame)) ==
      analysis::DerivedConstructorError::kInstruction);
  auto wrong_constructor = field_constructor;
  wrong_constructor[17] = 1;  // LDA newTarget, not the current constructor.
  Check(std::get<analysis::DerivedConstructorError>(
      analysis::ProveSpreadDerivedConstructor(Body(wrong_constructor), rest_frame)) ==
      analysis::DerivedConstructorError::kValue);
  auto wrong_guard = field_constructor;
  wrong_guard[27] = kRequireInitialized;
  Check(std::get<analysis::DerivedConstructorError>(
      analysis::ProveSpreadDerivedConstructor(Body(wrong_guard), rest_frame)) ==
      analysis::DerivedConstructorError::kGuard);
  auto invalid_frame = rest_frame;
  invalid_frame.new_target = invalid_frame.function;
  Check(std::get<analysis::DerivedConstructorError>(
      analysis::ProveSpreadDerivedConstructor(Body(field_constructor), invalid_frame)) ==
      analysis::DerivedConstructorError::kFrame);
  invalid_frame.new_target = abc::RegisterId{invalid_frame.registers};
  Check(std::get<analysis::DerivedConstructorError>(
      analysis::ProveSpreadDerivedConstructor(Body(field_constructor), invalid_frame)) ==
      analysis::DerivedConstructorError::kFrame);
  auto duplicate = field_constructor;
  duplicate.insert(duplicate.end() - 1, {kLoad, 0, 0xb9, 0, 5});
  Check(std::get<analysis::DerivedConstructorError>(
      analysis::ProveSpreadDerivedConstructor(Body(duplicate), rest_frame)) ==
      analysis::DerivedConstructorError::kInstruction);
  for (const std::size_t count : {1U, 2U, 200U, 201U, 400U, 800U}) {
    auto copies = field_constructor;
    // Shared constructor identity survives real accumulator/register copies.
    for (std::size_t i = 0; i < count; ++i)
      copies.insert(copies.begin() + 18, {kStore, 0, kLoad, 0});
    Check(std::holds_alternative<analysis::SpreadDerivedConstructor>(
        analysis::ProveSpreadDerivedConstructor(Body(copies), rest_frame)));
  }
  auto joined_code = field_constructor;
  joined_code.erase(joined_code.begin() + 16, joined_code.begin() + 18);
  const std::vector<std::uint8_t> fork{
      kLoad, 3, static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM8), 6,
      kLoad, 0, static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), 4,
      kLoad, 0};
  joined_code.insert(joined_code.begin() + 16, fork.begin(), fork.end());
  const auto branch_body = [&](const std::vector<std::uint8_t>& code) {
    auto body = Body(code);
    for (auto& instruction : body.instructions) {
      if (instruction.offset.value == 18) instruction.branch_target = abc::CodeOffset{24};
      if (instruction.offset.value == 22) instruction.branch_target = abc::CodeOffset{26};
    }
    return body;
  };
  // Equal origins join; a single newTarget incoming value poisons the phi.
  Check(std::holds_alternative<analysis::SpreadDerivedConstructor>(
      analysis::ProveSpreadDerivedConstructor(branch_body(joined_code), rest_frame)));
  joined_code[25] = 1;
  Check(std::get<analysis::DerivedConstructorError>(
      analysis::ProveSpreadDerivedConstructor(branch_body(joined_code), rest_frame)) ==
      analysis::DerivedConstructorError::kValue);
  const auto* fields =
      std::get_if<analysis::SpreadFieldDerivedConstructor>(&field_proof);
  Check(fields && fields->writes.size() == 1 &&
        fields->writes[0].name.value == 56 &&
        fields->writes[0].value.value == 31);
  auto wrong_field = field_constructor;
  wrong_field[46] = 3;
  Check(std::holds_alternative<analysis::DerivedConstructorError>(
      analysis::ProveSpreadFieldDerivedConstructor(
          Body(wrong_field), rest_frame)));
  auto wrong_rest = field_constructor;
  wrong_rest[9] = 1;
  Check(std::holds_alternative<analysis::DerivedConstructorError>(
      analysis::ProveSpreadFieldDerivedConstructor(
          Body(wrong_rest), rest_frame)));
  auto wrong_spread = field_constructor;
  wrong_spread[20] = 4;
  Check(std::get<analysis::DerivedConstructorError>(
            analysis::ProveSpreadFieldDerivedConstructor(
                Body(wrong_spread), rest_frame)) ==
        analysis::DerivedConstructorError::kValue);
  auto branched_field = Body(field_constructor);
  branched_field.instructions[0].branch_target = abc::CodeOffset{0};
  Check(std::get<analysis::DerivedConstructorError>(
            analysis::ProveSpreadFieldDerivedConstructor(
                branched_field, rest_frame)) ==
        analysis::DerivedConstructorError::kControlFlow);
  Check(std::get<analysis::DerivedConstructorError>(
            analysis::ProveSpreadFieldDerivedConstructor(
                Body(field_constructor), rest_frame, {0})) ==
        analysis::DerivedConstructorError::kResourceLimit);
  for (const std::size_t count : {200U, 201U, 400U, 800U}) {
    auto longer = field_constructor;
    longer.insert(longer.begin() + 12, count, kNop);
    Check(std::holds_alternative<analysis::SpreadFieldDerivedConstructor>(
        analysis::ProveSpreadFieldDerivedConstructor(
            Body(longer), rest_frame)));
  }
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
