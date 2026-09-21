#ifndef ABCD_ABC_FRAME_LAYOUT_H_
#define ABCD_ABC_FRAME_LAYOUT_H_

#include <optional>

#include "abcd/abc/register_access.h"

namespace abcd::abc {

struct CallFlags {
  bool this_slot = false;
  bool new_target_slot = false;
  bool extra_arguments = false;
  bool function_slot = false;
};

struct FrameLayout {
  std::uint32_t locals = 0;
  std::optional<RegisterId> function;
  std::optional<RegisterId> new_target;
  std::optional<RegisterId> this_value;
  // When parameters is zero this is an end position, not a readable register.
  RegisterId first_parameter;
  std::uint32_t parameters = 0;
  std::uint32_t registers = 0;
  bool extra_arguments = false;
};

enum class FrameError { kArgumentUnderflow, kRuntimeCountRange };

// OpenHarmony-v7.0-Release MethodLiteral ABI. nullopt is the runtime's missing
// annotation default, not an unknown value. A failed annotation read must never
// be passed here as nullopt. High bits are ignored exactly as in the runtime.
[[nodiscard]] CallFlags DecodeCallFlags(std::optional<std::uint32_t> raw);

// Constant time/storage, no allocation. arguments is CodeInfo::arguments and
// includes hidden prefix slots. This models the declared register window, not
// the entire physical call stack or actual argument list. Historical semantic
// equivalence and annotation selection remain the caller's responsibility.
[[nodiscard]] std::variant<FrameLayout, FrameError> BuildFrameLayout(
    std::uint32_t locals, std::uint32_t arguments, CallFlags flags);

}  // namespace abcd::abc
#endif  // ABCD_ABC_FRAME_LAYOUT_H_
