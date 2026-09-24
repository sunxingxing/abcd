#ifndef ABCD_ANALYSIS_VALUE_WRITE_H_
#define ABCD_ANALYSIS_VALUE_WRITE_H_

#include <cstdint>
#include <variant>

#include "abcd/abc/register_access.h"

namespace abcd::analysis {
struct ValueWrite {
  abc::RegisterId object;
  abc::RegisterId key;
};
enum class ValueWriteError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};

// Pinned v7 STOBJBYVALUE is strict [[Set]] with register receiver/key and
// accumulator value. It may throw and leaves the accumulator unchanged.
[[nodiscard]] std::variant<ValueWrite, ValueWriteError> LiftValueWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis

#endif  // ABCD_ANALYSIS_VALUE_WRITE_H_
