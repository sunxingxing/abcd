#ifndef ABCD_TEST_SHARED_EXIT_FIXTURE_H_
#define ABCD_TEST_SHARED_EXIT_FIXTURE_H_

#include "ssa_fixture.h"

#include <array>

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
}  // namespace abcd::test
#endif
