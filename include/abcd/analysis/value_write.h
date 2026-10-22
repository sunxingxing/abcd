#ifndef ABCD_ANALYSIS_VALUE_WRITE_H_
#define ABCD_ANALYSIS_VALUE_WRITE_H_

#include <cstdint>
#include <variant>

#include "abcd/analysis/scalar.h"
#include "abcd/analysis/named_read.h"

namespace abcd::analysis {
struct ValueWrite {
  abc::RegisterId object;
  ScalarValue key;
  PropertyLookup lookup = PropertyLookup::kObject;
};
enum class ValueWriteError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};

// Pinned v7 STOBJBYVALUE / STOBJBYINDEX are strict [[Set]] with register
// receiver and an already evaluated register key / exact uint32 constant, and
// accumulator value. It may throw and leaves the accumulator unchanged.
[[nodiscard]] std::variant<ValueWrite, ValueWriteError> LiftValueWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis

#endif  // ABCD_ANALYSIS_VALUE_WRITE_H_
