#ifndef ABCD_ANALYSIS_ORDINARY_CALL_H_
#define ABCD_ANALYSIS_ORDINARY_CALL_H_

#include <array>
#include <optional>

#include "abcd/abc/register_access.h"

namespace abcd::analysis {
inline constexpr std::size_t kFixedCallArguments = 3;
struct CallRegisters {
  std::array<abc::RegisterId, kFixedCallArguments> values{};
  std::uint8_t count = 0;
};
struct CallRange {
  abc::RegisterId first;
  std::uint16_t count = 0;
};
struct OrdinaryCall {
  std::variant<CallRegisters, CallRange> arguments;
  [[nodiscard]] std::uint32_t Count() const;
  [[nodiscard]] std::optional<abc::RegisterId> At(std::uint32_t position) const;
};
enum class CallError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};

// CALLARG0/1/2/3 and narrow CALLRANGE: acc callee, undefined
// receiver/new.target, register arguments, result replaces acc on success only.
// No .call property access or iterator is implied. Wide range has conflicting
// pinned interpreter offsets; explicit receiver/legacy calls need separate
// evidence. O(1) lift and argument access, fixed storage; zero range count
// reads no register.
[[nodiscard]] std::variant<OrdinaryCall, CallError> LiftOrdinaryCall(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_ORDINARY_CALL_H_
