#ifndef ABCD_ABC_REGISTER_ACCESS_H_
#define ABCD_ABC_REGISTER_ACCESS_H_

#include "abcd/abc/instruction.h"

namespace abcd::abc {
struct RegisterId {
  std::uint32_t value = 0;
  auto operator<=>(const RegisterId&) const = default;
};
struct RegisterWindow {
  RegisterId first;
  std::uint32_t count = 0;
  OperandAccess access = OperandAccess::kRead;
};
struct RegisterAccesses {
  std::array<RegisterWindow, kMaxInstructionOperands> windows{};
  std::uint8_t count = 0;
  bool accumulator_read = false;
  bool accumulator_write = false;
};
enum class RegisterAccessError {
  kMalformedInstruction,
  kInvalidCount,
  kFrameRange
};
using RegisterAccessResult =
    std::variant<RegisterAccesses, RegisterAccessError>;

// Requires an instruction from DecodeInstruction. Returns may-access windows,
// not unconditional execution effects or exceptional-edge definitions. Explicit
// accesses retain operand order and may overlap. No heap allocation or
// expansion by argument count. Hidden environment/heap/frame state is outside
// this API.
[[nodiscard]] RegisterAccessResult GetRegisterAccesses(
    const DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::abc
#endif
