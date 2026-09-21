#ifndef ABCD_ABC_CONTROL_TRANSFER_H_
#define ABCD_ABC_CONTROL_TRANSFER_H_

#include <cstdint>
#include <optional>

#include "abcd/abc/generated_isa.h"

namespace abcd::abc {

enum class ControlTransfer : std::uint8_t {
  kFallthrough,
  kJump,
  kBranch,
  kReturn,
  kThrow,
  kSuspend,
};

// Returns absence for an opcode outside the pinned ISA. This describes the
// normal control transfer; exception edges are determined separately.
[[nodiscard]] std::optional<ControlTransfer> GetControlTransfer(
    Opcode opcode) noexcept;

}  // namespace abcd::abc

#endif  // ABCD_ABC_CONTROL_TRANSFER_H_
