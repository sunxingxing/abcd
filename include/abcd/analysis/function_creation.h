#ifndef ABCD_ANALYSIS_FUNCTION_CREATION_H_
#define ABCD_ANALYSIS_FUNCTION_CREATION_H_

#include "abcd/abc/instruction.h"
#include "abcd/abc/pool_reference.h"

namespace abcd::analysis {
enum class CreationKind { kFunction, kMethod };
// kLiteralObject: an object-literal buffer method; CloneObjectLiteral installs
// the freshly created literal object as home object. Never an instruction input.
enum class HomeObjectInput { kCurrentFunction, kAccumulator, kLiteralObject };
struct FunctionLength {
  std::uint8_t value = 0;
  bool operator==(const FunctionLength&) const = default;
};
struct FunctionCreation {
  CreationKind kind;
  abc::PoolIndex method;
  FunctionLength length;
  HomeObjectInput home_object;
  std::uint16_t cache_slot = 0;
  bool operator==(const FunctionCreation&) const = default;
};
enum class CreationError { kUnsupportedOpcode, kMalformedInstruction };

// DecodeInstruction input required. Models DEFINEFUNC/DEFINEMETHOD inputs;
// the result is assigned to the accumulator. Both receive the current module
// and lexical environment. Target attributes determine actual allocation and
// capture: sendable DEFINEFUNC does NOT capture the ordinary lexical env/home.
// Method IDs are caller-regional pool slots, not entity offsets. Length is an
// observable property, not the target frame's parameter count. Preserve each
// dynamic creation occurrence; do not deduplicate function objects by method.
// The cache slot is retained for format fidelity, never source semantics.
// No allocation, target resolution or claim of final JS wrapper equivalence.
[[nodiscard]] std::variant<FunctionCreation, CreationError>
LiftFunctionCreation(const abc::DecodedInstruction& instruction);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_FUNCTION_CREATION_H_
