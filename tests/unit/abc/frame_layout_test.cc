#include "abcd/abc/frame_layout.h"

#include <cstdlib>
#include <limits>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
}  // namespace

int main() {
  using namespace abcd::abc;
  // Independent truth table: bit order is this, new-target, extra, function.
  for (std::uint32_t bits = 0; bits < 16; ++bits) {
    const auto flags = DecodeCallFlags(bits);
    Check(flags.this_slot == bool(bits & 1));
    Check(flags.new_target_slot == bool(bits & 2));
    Check(flags.extra_arguments == bool(bits & 4));
    Check(flags.function_slot == bool(bits & 8));
    for (const std::uint32_t locals : {0u, 200u, 201u, 65536u}) {
      const auto hidden = std::uint32_t(flags.function_slot) +
                          flags.new_target_slot + flags.this_slot;
      const auto frame =
          std::get<FrameLayout>(BuildFrameLayout(locals, hidden + 5, flags));
      std::uint32_t next = locals;
      Check(frame.function.has_value() == flags.function_slot);
      if (frame.function) Check(frame.function->value == next++);
      Check(frame.new_target.has_value() == flags.new_target_slot);
      if (frame.new_target) Check(frame.new_target->value == next++);
      Check(frame.this_value.has_value() == flags.this_slot);
      if (frame.this_value) Check(frame.this_value->value == next++);
      Check(frame.first_parameter.value == next && frame.parameters == 5);
      Check(frame.locals == locals && frame.registers == next + 5);
      Check(frame.extra_arguments == flags.extra_arguments);
      Check(std::get<FrameLayout>(BuildFrameLayout(locals, hidden, flags))
                .parameters == 0);
      if (hidden != 0)
        Check(
            std::get<FrameError>(BuildFrameLayout(locals, hidden - 1, flags)) ==
            FrameError::kArgumentUnderflow);
    }
  }
  const auto missing = DecodeCallFlags(std::nullopt);
  Check(missing.function_slot && missing.new_target_slot && missing.this_slot &&
        missing.extra_arguments);
  const auto masked = DecodeCallFlags(0xfffffff0u);
  Check(!masked.function_slot && !masked.new_target_slot && !masked.this_slot &&
        !masked.extra_arguments);
  constexpr std::uint32_t kRuntimeCountMax = (1u << 28) - 1;
  const auto maximum = std::get<FrameLayout>(
      BuildFrameLayout(kRuntimeCountMax, kRuntimeCountMax + 3, missing));
  Check(maximum.parameters == kRuntimeCountMax);
  Check(maximum.registers == 2 * kRuntimeCountMax + 3);
  Check(std::get<FrameError>(
            BuildFrameLayout(kRuntimeCountMax + 1, 3, missing)) ==
        FrameError::kRuntimeCountRange);
  Check(std::get<FrameError>(
            BuildFrameLayout(0, kRuntimeCountMax + 4, missing)) ==
        FrameError::kRuntimeCountRange);
  Check(std::get<FrameError>(BuildFrameLayout(
            std::numeric_limits<std::uint32_t>::max(), 3, missing)) ==
        FrameError::kRuntimeCountRange);
}
