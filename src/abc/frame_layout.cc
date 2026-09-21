#include "abcd/abc/frame_layout.h"

#include <limits>

namespace abcd::abc {
namespace {
// MethodLiteral::{HaveThisBit,HaveNewTargetBit,HaveExtraBit,HaveFuncBit,
// VREGS_ARGS_NUM_BITS}; see docs/reference/function-abi.md.
constexpr std::uint32_t kThisMask = 1u << 0;
constexpr std::uint32_t kNewTargetMask = 1u << 1;
constexpr std::uint32_t kExtraMask = 1u << 2;
constexpr std::uint32_t kFunctionMask = 1u << 3;
constexpr unsigned kRuntimeCountBits = 28;
constexpr std::uint32_t kRuntimeCountMax = (1u << kRuntimeCountBits) - 1;
}  // namespace

CallFlags DecodeCallFlags(std::optional<std::uint32_t> raw) {
  const auto bits = raw.value_or(std::numeric_limits<std::uint32_t>::max());
  return {(bits & kThisMask) != 0, (bits & kNewTargetMask) != 0,
          (bits & kExtraMask) != 0, (bits & kFunctionMask) != 0};
}

std::variant<FrameLayout, FrameError> BuildFrameLayout(std::uint32_t locals,
                                                       std::uint32_t arguments,
                                                       CallFlags flags) {
  const auto hidden = std::uint32_t(flags.function_slot) +
                      flags.new_target_slot + flags.this_slot;
  if (arguments < hidden) return FrameError::kArgumentUnderflow;
  const auto parameters = arguments - hidden;
  if (locals > kRuntimeCountMax || parameters > kRuntimeCountMax)
    return FrameError::kRuntimeCountRange;

  // Validate runtime widths before addition. The two 28-bit counts and at
  // most three hidden slots fit uint32_t without truncation or wraparound.
  FrameLayout frame;
  frame.locals = locals;
  auto next = locals;
  if (flags.function_slot) frame.function = RegisterId{next++};
  if (flags.new_target_slot) frame.new_target = RegisterId{next++};
  if (flags.this_slot) frame.this_value = RegisterId{next++};
  frame.first_parameter = RegisterId{next};
  frame.parameters = parameters;
  frame.registers = next + parameters;
  frame.extra_arguments = flags.extra_arguments;
  return frame;
}
}  // namespace abcd::abc
