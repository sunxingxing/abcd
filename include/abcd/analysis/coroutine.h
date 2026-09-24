#ifndef ABCD_ANALYSIS_COROUTINE_H_
#define ABCD_ANALYSIS_COROUTINE_H_

#include <cstdint>
#include <variant>

#include "abcd/abc/instruction.h"
#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
enum class CoroutineTransferKind { kAwait, kSuspend, kResolve, kReject };
struct CoroutineTransfer {
  CoroutineTransferKind kind;
  ScalarSlot state;
  ScalarSlot value;
};
enum class CoroutineReadKind { kResumeValue, kResumeMode };
struct CoroutineRead {
  CoroutineReadKind kind;
  ScalarSlot state;
};
enum class CoroutineError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};
using CoroutineResult =
    std::variant<CoroutineTransfer, CoroutineRead, CoroutineError>;

// Pinned v7 interpreter-inl.cpp: these are VM state operations, not ordinary
// calls or Language values. A suspend exits the current activation; its acc
// result is NOT the resumed value. Recover await only after proving the resume
// mode, rejection edge and exception ownership. Deprecated settlement's third
// operand needs a historical semantic profile and remains unsupported here.
[[nodiscard]] CoroutineResult LiftCoroutineInstruction(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_COROUTINE_H_
