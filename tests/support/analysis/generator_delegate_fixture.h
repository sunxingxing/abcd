#ifndef ABCD_TEST_GENERATOR_DELEGATE_FIXTURE_H_
#define ABCD_TEST_GENERATOR_DELEGATE_FIXTURE_H_

#include <array>
#include <vector>
#include "abcd/analysis/coroutine.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
// Pinned OH7 FunctionBuilder::YieldStar and Iterator::{Close,CallMethod}.
// Register scopes deliberately reuse comparison/close/rethrow temporaries.
// This is decoded bytecode, never a reference-source emitter.
inline constexpr std::uint8_t kDelegateLocals = 18;
enum class DelegateTruth { kNative, kRuntime, kRuntimeTrue, kRuntimeFalse };
struct DelegateFixture {
  std::uint32_t create, close_begin, close_end, close_catch, close_catch_end,
      end, next_copy, call, suspend, mode, loop_branch, closed_truth;
};
inline DelegateFixture AppendGeneratorDelegate(std::vector<std::uint8_t>& code,
    bool wide, DelegateTruth truth = DelegateTruth::kNative) {
  using abc::Opcode;
  enum class Label { kLoop, kThrow, kReturn, kCall, kComplete, kNormal,
                     kNoClose, kCloseEnd, kRethrowSkip, kCount };
  struct Jump { std::size_t pc; Label label; };
  std::array<std::size_t, static_cast<std::size_t>(Label::kCount)> labels{};
  std::vector<Jump> jumps;
  const auto op = [](Opcode value) { return static_cast<std::uint8_t>(value); };
  const auto emit = [&](std::initializer_list<std::uint8_t> bytes) { code.insert(code.end(), bytes); };
  const auto pc = [&] { return static_cast<std::uint32_t>(code.size()); };
  const auto label = [&](Label value) { labels[static_cast<std::size_t>(value)] = code.size(); };
  const auto branch = [&](bool conditional, bool inverse, Label target) {
    jumps.push_back({code.size(), target});
    code.push_back(op(conditional ? (wide ? (inverse ? Opcode::kJNEZ_IMM32 : Opcode::kJEQZ_IMM32) :
        (inverse ? Opcode::kJNEZ_IMM16 : Opcode::kJEQZ_IMM16)) : (wide ? Opcode::kJMP_IMM32 : Opcode::kJMP_IMM16)));
    code.insert(code.end(), wide ? 4 : 2, 0);
  };
  const auto load = [&](std::uint8_t reg) { emit({op(Opcode::kLDA_V8), reg}); };
  const auto store = [&](std::uint8_t reg) { emit({op(Opcode::kSTA_V8), reg}); };
  const auto move = [&](std::uint8_t to, std::uint8_t from) { emit({op(Opcode::kMOV_V8_V8), to, from}); };
  const auto constant = [&](Opcode value, std::uint8_t reg) { emit({op(value)}); store(reg); };
  const auto boolean = [&](bool positive) {
    const bool runtime = truth == DelegateTruth::kRuntime ||
        (positive ? truth == DelegateTruth::kRuntimeTrue : truth == DelegateTruth::kRuntimeFalse);
    if (runtime) {
      const auto instruction = static_cast<std::uint16_t>(positive ?
          Opcode::kCALLRUNTIME_ISTRUE_PREF_IMM8 : Opcode::kCALLRUNTIME_ISFALSE_PREF_IMM8);
      emit({static_cast<std::uint8_t>(instruction), static_cast<std::uint8_t>(instruction >> 8), 0});
    } else emit({op(positive ? Opcode::kISTRUE : Opcode::kISFALSE)});
  };
  const auto name = [&](std::uint8_t object, std::uint8_t slot) {
    load(object); emit({op(Opcode::kLDOBJBYNAME_IMM8_ID16), 0, slot, 0});
  };
  const auto equal = [&](bool strict, std::uint8_t reg) { emit({op(strict ? Opcode::kSTRICTEQ_IMM8_V8 : Opcode::kEQ_IMM8_V8), 0, reg}); };
  const auto mode = [&](analysis::CoroutineResumeMode value) {
    emit({op(Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(value), 0, 0, 0});
  };
  const auto throwing = [&] {
    constexpr auto value = static_cast<std::uint16_t>(Opcode::kTHROW_PREF_NONE);
    emit({static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8)});
  };
  const auto guard = [&](std::uint8_t value) {
    constexpr auto instruction = static_cast<std::uint16_t>(Opcode::kTHROW_IFNOTOBJECT_PREF_V8);
    emit({static_cast<std::uint8_t>(instruction), static_cast<std::uint8_t>(instruction >> 8), value});
  };
  DelegateFixture result{};
  load(kDelegateLocals + 3); // ABI user argument; initial handshake precedes it.
  result.create = pc(); emit({op(Opcode::kGETITERATOR_IMM8), 0}); store(5);
  name(5, 3); store(4); constant(Opcode::kLDFALSE, 3);
  constant(Opcode::kLDUNDEFINED, 6); mode(analysis::CoroutineResumeMode::kNext); store(7);
  result.next_copy = pc(); move(8, 4);
  label(Label::kLoop); constant(Opcode::kLDFALSE, 9);
  mode(analysis::CoroutineResumeMode::kNext); equal(true, 7); branch(true, false, Label::kThrow);
  move(4, 8); branch(false, false, Label::kCall);
  label(Label::kThrow); mode(analysis::CoroutineResumeMode::kThrow); equal(true, 7); branch(true, false, Label::kReturn);
  name(5, 4); store(4); store(11); emit({op(Opcode::kLDUNDEFINED)}); equal(false, 11); branch(true, false, Label::kCall);
  store(11); load(3); result.closed_truth = pc(); boolean(true); branch(true, true, Label::kNoClose);
  constant(Opcode::kLDTRUE, 3); constant(Opcode::kLDUNDEFINED, 12); constant(Opcode::kLDHOLE, 13);
  result.close_begin = pc(); name(5, 5); store(4); store(14); emit({op(Opcode::kLDUNDEFINED)}); equal(false, 14);
  branch(true, true, Label::kNoClose); load(4); emit({op(Opcode::kCALLTHIS0_IMM8_V8), 0, 5}); store(12);
  result.close_end = pc(); branch(false, false, Label::kCloseEnd);
  result.close_catch = pc(); store(13); label(Label::kCloseEnd); result.close_catch_end = pc();
  load(13); store(14); constant(Opcode::kLDHOLE, 15); load(14); emit({op(Opcode::kNOTEQ_IMM8_V8), 0, 15});
  branch(true, false, Label::kRethrowSkip); load(14); throwing(); label(Label::kRethrowSkip);
  load(12); guard(12); label(Label::kNoClose); load(11);
  constexpr auto missing_throw = static_cast<std::uint16_t>(Opcode::kTHROW_NOTEXISTS_PREF_NONE);
  emit({static_cast<std::uint8_t>(missing_throw), static_cast<std::uint8_t>(missing_throw >> 8)});
  label(Label::kReturn); constant(Opcode::kLDTRUE, 9); name(5, 5); store(4); store(11);
  emit({op(Opcode::kLDUNDEFINED)}); equal(false, 11); branch(true, false, Label::kCall);
  load(6); emit({op(Opcode::kRETURN)});
  label(Label::kCall); load(4); result.call = pc(); emit({op(Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 5, 6}); store(6); guard(6);
  name(6, 6); boolean(true); branch(true, true, Label::kComplete);
  load(6); result.suspend = pc(); emit({op(Opcode::kSUSPENDGENERATOR_V8), 0}); load(0); emit({op(Opcode::kRESUMEGENERATOR)}); store(6);
  load(0); result.mode = pc(); emit({op(Opcode::kGETRESUMEMODE)}); store(7);
  result.loop_branch = pc(); branch(false, false, Label::kLoop);
  label(Label::kComplete); load(9); boolean(false); branch(true, true, Label::kNormal);
  name(6, 7); emit({op(Opcode::kRETURN)}); label(Label::kNormal); name(6, 7);
  result.end = pc();
  for (const auto& jump : jumps) {
    const auto delta = std::int64_t(labels[static_cast<std::size_t>(jump.label)]) - std::int64_t(jump.pc);
    Check(wide || (delta >= INT16_MIN && delta <= INT16_MAX));
    for (unsigned byte = 0; byte < (wide ? 4U : 2U); ++byte)
      code[jump.pc + 1 + byte] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(delta) >> (byte * 8));
  }
  return result;
}
}  // namespace abcd::test
#endif
