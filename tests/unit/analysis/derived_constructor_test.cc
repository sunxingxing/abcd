#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/register_effect.h"
#include "abcd/analysis/derived_constructor.h"

#include <array>
#include <cstdlib>
#include <vector>
#include <iostream>
#include <source_location>

namespace {
void Check(bool condition, std::source_location where = std::source_location::current()) {
  if (!condition) { std::cerr << where.line() << '\n'; std::abort(); }
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
enum class Region { kStraight, kBefore, kAfter, kThroughCall, kBackwardLayout };
enum class Label { kLoop, kExit, kCall, kTail, kCount };
class InitializationCode {
 public:
  void Emit(abcd::abc::Opcode opcode,
            std::initializer_list<std::uint8_t> operands = {}) {
    const auto wire = static_cast<std::uint16_t>(opcode);
    bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
    bytes.insert(bytes.end(), operands.begin(), operands.end());
  }
  void Mark(Label label) {
    labels[static_cast<std::size_t>(label)] = bytes.size();
  }
  void Jump(abcd::abc::Opcode opcode, Label target) {
    fixes.push_back({bytes.size(), target});
    Emit(opcode, {0, 0, 0, 0});
  }
  abcd::abc::MethodBody Decode(abcd::abc::ProfileId profile) {
    for (const auto [pc, target] : fixes) {
      const auto delta = static_cast<std::uint32_t>(
          labels[static_cast<std::size_t>(target)] - pc);
      for (std::size_t byte = 0; byte < sizeof(delta); ++byte)
        bytes[pc + 1 + byte] = static_cast<std::uint8_t>(delta >> (8 * byte));
    }
    abcd::abc::MethodBody body;
    for (std::uint32_t pc = 0; pc < bytes.size();) {
      const auto decoded = std::get<abcd::abc::DecodedInstruction>(
          abcd::abc::DecodeInstruction(std::span(bytes).subspan(pc), profile));
      abcd::abc::BodyInstruction site{{pc}, decoded, {}};
      if (decoded.branch_delta)
        site.branch_target = abcd::abc::CodeOffset{
            static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
      body.instructions.push_back(site);
      pc += decoded.size;
    }
    return body;
  }

 private:
  std::vector<std::uint8_t> bytes;
  std::array<std::size_t, static_cast<std::size_t>(Label::kCount)> labels{};
  std::vector<std::pair<std::size_t, Label>> fixes;
};
void NativeInitializationFixtures() {
  using namespace abcd;
  using namespace analysis;
  using abc::Opcode;
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(4, 4, abc::CallFlags{true, true, false, true}));
  const auto receiver = static_cast<std::uint8_t>(frame.this_value->value);
  const auto parameter = static_cast<std::uint8_t>(frame.first_parameter.value);
  std::uint64_t previous_work = 0;
  for (const unsigned count : {1U, 2U, 200U, 201U, 400U, 800U}) {
    InitializationCode code;
    for (unsigned i = 0; i < count; ++i) {
      code.Emit(Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8, {0, 1, parameter});
      code.Emit(Opcode::kSTA_V8, {0});
      code.Emit(Opcode::kLDA_V8, {receiver});
      code.Emit(Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8, {1});
      code.Emit(Opcode::kLDA_V8, {0});
      code.Emit(Opcode::kSTA_V8, {receiver});
    }
    code.Emit(Opcode::kLDA_V8, {receiver});
    code.Emit(Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8, {0});
    code.Emit(Opcode::kRETURN);
    auto body = code.Decode(abc::ProfileId::kDynamic1301);
    const auto checked = ProveRangeDerivedConstructor(body, frame);
    if (const auto* error = std::get_if<DerivedConstructorError>(&checked))
      std::cerr << "native proof " << count << " "
                << static_cast<unsigned>(*error) << '\n';
    Check(std::holds_alternative<RangeDerivedConstructor>(checked));
    Check(std::get<RangeDerivedConstructor>(checked).sites.size() == count);
    const auto work = std::get<RangeDerivedConstructor>(checked).work;
    if (count >= 400) {
      constexpr std::uint64_t kMaxDoublingGrowth = 3;
      Check(work < previous_work * kMaxDoublingGrowth);
    }
    previous_work = work;
    std::cout << "native range sites " << count << " work " << work << '\n';
    Check(std::get<DerivedConstructorError>(ProveRangeDerivedConstructor(
              body, frame, {0})) == DerivedConstructorError::kResourceLimit);
    Check(std::get<DerivedConstructorError>(ProveRangeDerivedConstructor(
              body, frame, {DerivedConstructorBudget::kDefaultBytes, 0})) ==
          DerivedConstructorError::kResourceLimit);
    if (count > 1)
      Check(std::get<RangeDerivedConstructor>(checked).coverage[0].action ==
            DerivedCheckAction::kBind);
    // Changing the actual binding guard to an initialized check must not
    // authorize native super: it would throw on the first invocation.
    for (auto& site : body.instructions)
      if (site.decoded.opcode ==
          Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8) {
        site.decoded.operands[0].raw_bits = 0;
        break;
      }
    const auto refused = ProveRangeDerivedConstructor(body, frame);
    Check(std::holds_alternative<DerivedConstructorError>(refused));
    Check(std::get<DerivedConstructorError>(refused) ==
          (count == 1 ? DerivedConstructorError::kGuard
                      : DerivedConstructorError::kInstruction));
  }
  // Share a binding guard after an exclusive selection. Neither construction
  // dominates the join; copy/phi equations, not a one-producer restriction,
  // establish the common receiver protocol.
  {
    InitializationCode shared;
    shared.Emit(Opcode::kLDA_V8, {parameter});
    shared.Jump(Opcode::kJEQZ_IMM32, Label::kCall);
    shared.Emit(Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8, {0, 1, parameter});
    shared.Emit(Opcode::kSTA_V8, {0});
    shared.Jump(Opcode::kJMP_IMM32, Label::kTail);
    shared.Mark(Label::kCall);
    shared.Emit(Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8, {0, 0, parameter});
    shared.Emit(Opcode::kSTA_V8, {0});
    shared.Mark(Label::kTail);
    shared.Emit(Opcode::kLDA_V8, {receiver});
    shared.Emit(Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8, {1});
    shared.Emit(Opcode::kLDA_V8, {0});
    shared.Emit(Opcode::kSTA_V8, {receiver});
    shared.Emit(Opcode::kLDA_V8, {receiver});
    shared.Emit(Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8, {0});
    shared.Emit(Opcode::kRETURN);
    const auto result = ProveRangeDerivedConstructor(
        shared.Decode(abc::ProfileId::kDynamic1301), frame);
    Check(std::holds_alternative<RangeDerivedConstructor>(result));
    Check(std::get<RangeDerivedConstructor>(result).bindings.size() == 2);
  }
  {
    InitializationCode loop;
    loop.Mark(Label::kLoop);
    loop.Emit(Opcode::kLDA_V8, {parameter});
    loop.Jump(Opcode::kJEQZ_IMM32, Label::kExit);
    loop.Emit(Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8, {0, 1, parameter});
    loop.Emit(Opcode::kSTA_V8, {0});
    loop.Emit(Opcode::kLDA_V8, {receiver});
    loop.Emit(Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8, {1});
    loop.Emit(Opcode::kLDA_V8, {0});
    loop.Emit(Opcode::kSTA_V8, {receiver});
    loop.Jump(Opcode::kJMP_IMM32, Label::kLoop);
    loop.Mark(Label::kExit);
    loop.Emit(Opcode::kLDA_V8, {receiver});
    loop.Emit(Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8, {0});
    loop.Emit(Opcode::kRETURN);
    Check(std::holds_alternative<RangeDerivedConstructor>(
        ProveRangeDerivedConstructor(loop.Decode(abc::ProfileId::kDynamic1301),
                                     frame)));
  }
  InitializationCode code;
  code.Emit(Opcode::kLDA_V8, {parameter});
  code.Jump(Opcode::kJEQZ_IMM32, Label::kExit);
  code.Emit(Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8, {0, 1, parameter});
  code.Emit(Opcode::kSTA_V8, {0});
  code.Emit(Opcode::kLDA_V8, {receiver});
  code.Emit(Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8, {1});
  code.Emit(Opcode::kLDA_V8, {0});
  code.Emit(Opcode::kSTA_V8, {receiver});
  code.Mark(Label::kExit);
  code.Emit(Opcode::kLDA_V8, {receiver});
  code.Emit(Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8, {0});
  code.Emit(Opcode::kRETURN);
  Check(std::holds_alternative<RangeDerivedConstructor>(
      ProveRangeDerivedConstructor(code.Decode(abc::ProfileId::kDynamic1301),
                                   frame)));
  auto unchecked = code.Decode(abc::ProfileId::kDynamic1301);
  for (auto& instruction : unchecked.instructions)
    if (instruction.decoded.opcode ==
            Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8 &&
        instruction.decoded.operands[0].raw_bits == 0)
      instruction.decoded.operands[0].raw_bits = 2;
  // A raw undefined completion without the receiver check must not acquire
  // native ReferenceError semantics merely because this is a constructor.
  Check(std::get<DerivedConstructorError>(ProveRangeDerivedConstructor(
            unchecked, frame)) == DerivedConstructorError::kReturn);
}
void InitializationFixtures() {
  using namespace abcd;
  using namespace analysis;
  using abc::Opcode;
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(4, 4, abc::CallFlags{true, true, false, true}));
  const auto constructor = static_cast<std::uint8_t>(frame.function->value);
  const auto receiver = static_cast<std::uint8_t>(frame.this_value->value);
  const auto parameter = static_cast<std::uint8_t>(frame.first_parameter.value);
  std::size_t cases = 0;
  for (const auto profile :
       {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
        abc::ProfileId::kDynamic1301})
    for (const auto region :
         {Region::kStraight, Region::kBefore, Region::kAfter,
          Region::kThroughCall, Region::kBackwardLayout})
      for (const bool spread : {false, true})
        for (const bool guards : {false, true})
          for (const unsigned count : {0U, 1U, 200U, 201U, 402U, 804U})
            for (const std::uint8_t saved : {0U, 2U}) {
              InitializationCode code;
              const auto padding = [&] {
                for (unsigned i = 0; i < count; ++i) code.Emit(Opcode::kNOP);
              };
              const auto loop = [&] {
                code.Mark(Label::kLoop);
                code.Emit(Opcode::kLDA_V8, {parameter});
                code.Jump(Opcode::kJEQZ_IMM32, Label::kExit);
                padding();
                code.Jump(Opcode::kJMP_IMM32, Label::kLoop);
                code.Mark(Label::kExit);
              };
              if (region == Region::kBackwardLayout) {
                code.Jump(Opcode::kJMP_IMM32, Label::kCall);
                code.Mark(Label::kTail);
                padding();
                code.Emit(Opcode::kLDA_V8, {saved});
                code.Emit(Opcode::kRETURN);
                code.Mark(Label::kCall);
              }
              if (region == Region::kBefore) loop();
              if (region == Region::kThroughCall) {
                code.Mark(Label::kLoop);
                padding();
              }
              if (guards) {
                code.Emit(Opcode::kLDA_V8, {receiver});
                code.Emit(Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8, {1});
                // A runtime no-op does not require super to precede it.
                code.Emit(Opcode::kLDA_V8, {parameter});
                code.Emit(Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8, {2});
              }
              if (spread) {
                code.Emit(Opcode::kLDA_V8, {constructor});
                code.Emit(Opcode::kSUPERCALLSPREAD_IMM8_V8, {0, parameter});
              } else
                code.Emit(Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8,
                          {0, 1, parameter});
              code.Emit(Opcode::kSTA_V8, {saved});
              if (guards)
                code.Emit(Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8, {0});
              if (region == Region::kAfter) loop();
              if (region == Region::kStraight) padding();
              if (region == Region::kThroughCall) {
                code.Emit(Opcode::kLDA_V8, {parameter});
                code.Jump(Opcode::kJEQZ_IMM32, Label::kLoop);
              }
              if (region == Region::kBackwardLayout)
                code.Jump(Opcode::kJMP_IMM32, Label::kTail);
              else {
                code.Emit(Opcode::kLDA_V8, {saved});
                code.Emit(Opcode::kRETURN);
              }
              const auto body = code.Decode(profile);
              const bool accept = region != Region::kThroughCall;
              if (spread)
                {
                  const auto proof = ProveSpreadDerivedConstructor(body, frame, {}, profile);
                  Check(std::holds_alternative<SpreadDerivedConstructor>(proof) == accept);
                  if (!accept) Check(std::get<DerivedConstructorError>(proof) == DerivedConstructorError::kControlFlow);
                }
              else {
                const auto proof = ProveRangeDerivedConstructor(body, frame, {}, profile);
                Check(std::holds_alternative<RangeDerivedConstructor>(proof) == accept);
                if (!accept) Check(std::get<DerivedConstructorError>(proof) == DerivedConstructorError::kControlFlow);
              }
              ++cases;
            }
  // The work boundary is measured on real decoding/SSA, independently of the
  // implementation's queue sizes. A budget below the first success must fail.
  InitializationCode code;
  code.Emit(Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8, {0, 1, parameter});
  code.Emit(Opcode::kRETURN);
  const auto body = code.Decode(abc::ProfileId::kDynamic1301);
  std::uint64_t lower = 0, upper = 1;
  const auto accepts = [&](std::uint64_t work) {
    return std::holds_alternative<RangeDerivedConstructor>(ProveRangeDerivedConstructor(
        body, frame, {DerivedConstructorBudget::kDefaultBytes, work}));
  };
  while (!accepts(upper)) { Check(upper < 1000000); upper *= 2; }
  while (upper - lower > 1) {
    const auto middle = lower + (upper - lower) / 2;
    if (accepts(middle)) upper = middle; else lower = middle;
  }
  Check(accepts(upper));
  Check(std::get<DerivedConstructorError>(ProveRangeDerivedConstructor(
      body, frame, {DerivedConstructorBudget::kDefaultBytes, upper - 1})) == DerivedConstructorError::kResourceLimit);
  // Grow the CFG, not just an instruction list in one block. The encoded
  // offset is the specified IMM32 branch width (opcode plus int32 payload).
  constexpr auto kNextBranch = std::uint8_t{1 + sizeof(std::int32_t)};
  for (const unsigned count : {1U, 200U, 201U, 402U, 804U}) {
    InitializationCode chain;
    chain.Emit(Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8, {0, 1, parameter});
    chain.Emit(Opcode::kSTA_V8, {0});
    for (unsigned i = 0; i < count; ++i)
      chain.Emit(Opcode::kJMP_IMM32, {kNextBranch, 0, 0, 0});
    chain.Emit(Opcode::kLDA_V8, {0}); chain.Emit(Opcode::kRETURN);
    Check(std::holds_alternative<RangeDerivedConstructor>(
        ProveRangeDerivedConstructor(chain.Decode(abc::ProfileId::kDynamic1301), frame)));
  }
  std::cout << "decoded initialization cases " << cases << '\n';
}
}  // namespace

int main() {
  NativeInitializationFixtures();
  InitializationFixtures();
  for (const auto profile : {abcd::abc::ProfileId::kDynamic1206, abcd::abc::ProfileId::kDynamic1301, abcd::abc::ProfileId::kDynamic1202}) {
    using namespace abcd; using namespace analysis;
    const auto wire = static_cast<std::uint16_t>(abc::Opcode::kCALLRUNTIME_SUPERCALLFORWARDALLARGS_PREF_V8);
    const std::array<std::uint8_t,3> bytes{static_cast<std::uint8_t>(wire), static_cast<std::uint8_t>(wire >> 8), 5};
    const auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(bytes, profile));
    const auto lifted = LiftSemanticInstruction({{0},decoded,{}}, profile, 10);
    Check(std::holds_alternative<SemanticInstruction>(lifted));
    const auto& instruction = std::get<SemanticInstruction>(lifted);
    Check(std::get<SuperForward>(instruction.operation).constructor.value == 5);
    Check((instruction.effects.normal | LanguageEffect::kArgumentsRead) == instruction.effects.normal);
    Check(instruction.effects.normal == instruction.effects.before_throw &&
          instruction.effects.exceptional == ExceptionalCompletion::kMayThrow);
    const auto effect = std::get<RegisterEffect>(LiftRegisterEffect(decoded, 10));
    Check(effect.read_count == 1 && std::get<abc::RegisterId>(effect.reads[0]).value == 5 &&
          effect.write && std::holds_alternative<Accumulator>(*effect.write));
  }

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
