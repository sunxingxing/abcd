#ifndef ABCD_ANALYSIS_FUNCTION_CREATION_H_
#define ABCD_ANALYSIS_FUNCTION_CREATION_H_

#include "abcd/abc/instruction.h"
#include "abcd/abc/pool_reference.h"

namespace abcd::analysis {
enum class CreationKind { kFunction, kMethod };
enum class HomeObjectInput { kCurrentFunction, kAccumulator };
struct FunctionLength {
  std::uint8_t value = 0;
};
struct FunctionCreation {
  CreationKind kind;
  abc::PoolIndex method;
  FunctionLength length;
  HomeObjectInput home_object;
};
enum class CreationError { kUnsupportedOpcode, kMalformedInstruction };

// DecodeInstruction input required. Models DEFINEFUNC/DEFINEMETHOD inputs;
// the result is assigned to the accumulator. Both receive the current module
// and lexical environment. Target attributes determine actual allocation and
// capture: sendable DEFINEFUNC does NOT capture the ordinary lexical env/home.
// Method IDs are caller-regional pool slots, not entity offsets. Length is an
// observable property, not the target frame's parameter count. Preserve each
// dynamic creation occurrence; do not deduplicate function objects by method.
// No allocation, target resolution or claim of final JS wrapper equivalence.
[[nodiscard]] std::variant<FunctionCreation, CreationError>
LiftFunctionCreation(const abc::DecodedInstruction& instruction);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_FUNCTION_CREATION_H_
