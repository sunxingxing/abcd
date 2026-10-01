#ifndef ABCD_TEST_SHARED_EXIT_FIXTURE_H_
#define ABCD_TEST_SHARED_EXIT_FIXTURE_H_

#include "ssa_fixture.h"

#include <array>
#include <bit>

namespace abcd::test {
inline abc::MethodBody SharedExitBody() {
  enum class Label : std::uint8_t { kHeader, kSecond, kTail, kAfter, kCount };
  enum class Argument : std::uint8_t { kCondition, kChoose, kFirst, kSecond, kSide, kAfter, kPayload };
  struct Patch { std::size_t pc; Label target; };
  std::array<std::size_t, static_cast<std::size_t>(Label::kCount)> labels{};
  std::vector<Patch> patches;
  std::vector<std::uint8_t> bytes;
  const auto opcode = [&](abc::Opcode op) { bytes.push_back(static_cast<std::uint8_t>(op)); };
  const auto label = [&](Label id) { labels[static_cast<std::size_t>(id)] = bytes.size(); };
  const auto jump = [&](abc::Opcode op, Label target) {
    patches.push_back({bytes.size(), target}); opcode(op); bytes.push_back(0);
  };
  const auto call = [&](Argument argument) {
    opcode(abc::Opcode::kLDA_V8); bytes.push_back(static_cast<std::uint8_t>(argument));
    opcode(abc::Opcode::kCALLARG0_IMM8); bytes.push_back(0);
  };
  // while (condition()) {
  //   if (choose()) { if (first()) continue; }
  //   else { if (second()) continue; }
  //   side(); break;
  // }
  // after(); return payload;
  // The compiler shares the side/break arm between both nested guards.
  label(Label::kHeader); call(Argument::kCondition);
  jump(abc::Opcode::kJEQZ_IMM8, Label::kAfter);
  call(Argument::kChoose); jump(abc::Opcode::kJEQZ_IMM8, Label::kSecond);
  call(Argument::kFirst); jump(abc::Opcode::kJNEZ_IMM8, Label::kHeader);
  jump(abc::Opcode::kJMP_IMM8, Label::kTail);
  label(Label::kSecond); call(Argument::kSecond);
  jump(abc::Opcode::kJNEZ_IMM8, Label::kHeader);
  jump(abc::Opcode::kJMP_IMM8, Label::kTail);
  label(Label::kTail); call(Argument::kSide);
  jump(abc::Opcode::kJMP_IMM8, Label::kAfter);
  label(Label::kAfter); call(Argument::kAfter);
  opcode(abc::Opcode::kLDA_V8); bytes.push_back(static_cast<std::uint8_t>(Argument::kPayload));
  opcode(abc::Opcode::kRETURN);
  for (const auto patch : patches) {
    const auto distance = static_cast<std::int64_t>(labels[static_cast<std::size_t>(patch.target)]) -
        static_cast<std::int64_t>(patch.pc);
    Check(distance >= INT8_MIN && distance <= INT8_MAX);
    bytes[patch.pc + 1] = static_cast<std::uint8_t>(distance);
  }
  return Body(bytes);
}
inline abc::MethodBody SharedDecisionBody(std::uint32_t count, bool select_false, bool calls = false) {
  // Distinct strict comparisons share one selected() arm. The optional calls
  // exercise the still-unproved conditional snapshot boundary as a refusal.
  // Wide jumps keep 200/201 about graph size, not immediate-width overflow.
  struct Patch { std::size_t pc; bool selected; };
  std::vector<Patch> patches;
  std::vector<std::uint8_t> bytes;
  const auto op = [&](abc::Opcode code) { bytes.push_back(static_cast<std::uint8_t>(code)); };
  const auto call = [&](std::uint8_t argument) {
    op(abc::Opcode::kLDA_V8); bytes.push_back(argument);
    op(abc::Opcode::kCALLARG0_IMM8); bytes.push_back(0);
  };
  const auto jump = [&](abc::Opcode code, bool selected) {
    patches.push_back({bytes.size(), selected}); op(code);
    bytes.insert(bytes.end(), sizeof(std::int32_t), 0);
  };
  Check(count != 0 && count <= static_cast<std::uint32_t>(INT32_MAX));
  constexpr unsigned kByteBits = 8;
  for (std::uint32_t i = 0; i < count; ++i) {
    // Keep a real jump block between normal predicates too; its PC anchor and
    // transfer must survive conditional ownership, without an invented effect.
    if (i != 0) { op(abc::Opcode::kJMP_IMM8); bytes.push_back(2); }
    if (calls) call(0);
    else {
      op(abc::Opcode::kLDAI_IMM32);
      for (unsigned byte = 0; byte < sizeof(i); ++byte)
        bytes.push_back(static_cast<std::uint8_t>(i >> (byte * kByteBits)));
      op(select_false ? abc::Opcode::kSTRICTNOTEQ_IMM8_V8 : abc::Opcode::kSTRICTEQ_IMM8_V8); bytes.push_back(0); bytes.push_back(0);
    }
    jump(select_false ? abc::Opcode::kJEQZ_IMM32 : abc::Opcode::kJNEZ_IMM32, true);
  }
  jump(abc::Opcode::kJMP_IMM32, false);
  const auto selected = bytes.size(); call(1);
  const auto join = bytes.size(); call(2);
  op(abc::Opcode::kLDA_V8); bytes.push_back(3); op(abc::Opcode::kRETURN);
  for (const auto patch : patches) {
    const auto distance = (patch.selected ? selected : join) - patch.pc;
    Check(distance <= static_cast<std::size_t>(INT32_MAX));
    const auto delta = static_cast<std::int32_t>(distance);
    const auto bits = std::bit_cast<std::uint32_t>(delta);
    for (unsigned byte = 0; byte < sizeof(delta); ++byte)
      bytes[patch.pc + 1 + byte] = static_cast<std::uint8_t>(bits >> (byte * kByteBits));
  }
  return Body(bytes);
}
}  // namespace abcd::test
#endif
