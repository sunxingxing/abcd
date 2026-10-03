#ifndef ABCD_ANALYSIS_CONSTRUCTOR_CALL_H_
#define ABCD_ANALYSIS_CONSTRUCTOR_CALL_H_

#include <optional>
#include <variant>

#include "abcd/analysis/ordinary_call.h"

namespace abcd::analysis {
struct ConstructorCall {
  // The first register is the constructor; remaining registers are arguments.
  CallRange inputs;
  std::optional<std::uint16_t> profile_slot;

  [[nodiscard]] std::uint32_t ArgumentCount() const;
  [[nodiscard]] std::optional<abc::RegisterId> ArgumentAt(
      std::uint32_t position) const;
};
struct ApplyConstructorCall {
  abc::RegisterId constructor;
  // The accumulator contains one aggregate JSArray, not one argument.
  std::uint16_t profile_slot;
};
enum class ConstructorCallError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kEmptyRange,
  kRegisterRange
};

// Pinned v7 NEWOBJRANGE forms. The encoded count includes the constructor,
// while JS argument count excludes it; newTarget is that same constructor.
// Reject count zero before the runtime's count-minus-one path can underflow.
// Historical source admission still requires an enclosing profile proof.
[[nodiscard]] std::variant<ConstructorCall, ConstructorCallError>
LiftConstructorCall(const abc::DecodedInstruction& instruction,
                    std::uint64_t frame_size);
// RuntimeNewObjApply reads JSArray length/indices, never an iterator. Source
// argument expansion additionally requires a private complete array proof.
[[nodiscard]] std::variant<ApplyConstructorCall, ConstructorCallError>
LiftApplyConstructorCall(const abc::DecodedInstruction& instruction,
                        std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_CONSTRUCTOR_CALL_H_
