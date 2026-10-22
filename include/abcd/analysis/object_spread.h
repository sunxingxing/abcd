#ifndef ABCD_ANALYSIS_OBJECT_SPREAD_H_
#define ABCD_ANALYSIS_OBJECT_SPREAD_H_

#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
// OH7 interpreter-inl.cpp:5464,5489 / runtime_stubs-inl.h:453,626. On success acc is
// exactly object; on failure acc has its pre-Commit value. Never erase the
// operation when using this normal-result identity.
// Literal target mutations share allocation identity/order proofs, but retain
// distinct descriptor-copy versus [[Prototype]] effects through recovery.
enum class ObjectMutationKind : std::uint8_t { kCopyProperties, kSetPrototype };
struct CopyProperties {
  ScalarValue object;
  ScalarValue source;
  ObjectMutationKind kind = ObjectMutationKind::kCopyProperties;
};
[[nodiscard]] std::variant<CopyProperties, ScalarError> LiftCopyProperties(
    const abc::DecodedInstruction& instruction, std::uint64_t frame);
}  // namespace abcd::analysis
#endif
