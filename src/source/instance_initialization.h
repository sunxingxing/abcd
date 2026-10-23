#ifndef ABCD_SOURCE_INSTANCE_INITIALIZATION_H_
#define ABCD_SOURCE_INSTANCE_INITIALIZATION_H_

#include <variant>

#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_body.h"
#include "abcd/analysis/lexical.h"

namespace abcd::source {
enum class InstanceInitializationError : std::uint8_t {
  kPrefix, kLoad, kCall, kReceiver, kAccumulator, kReentry, kDuplicate
};
// Base class constructor whose class creator stored its instance initializer
// in depth-0 `slot` of the constructor's closure environment (es2panda
// function.cpp: CALLINIT at base-constructor entry). Requires the entry
// prefix to be register moves only, then exactly `LDLEXVAR 0, slot;
// CALLRUNTIME.CALLINIT imm, vThis` (interpreter-inl.cpp:1499, CALLTHIS0
// semantics) with the frame's this, a following instruction that does not
// read the accumulator, no branch or handler entering at or before the call,
// and no other load of the slot. Both instructions become NOPs of the same
// size: native field initialization happens at exactly this point of
// [[Construct]], before any constructor statement. Returns the call's index.
// O(instructions + registers), no retained allocation beyond the caller's body.
[[nodiscard]] std::variant<std::size_t, InstanceInitializationError> ProjectInstanceInitialization(
    abc::MethodBody& body, const abc::FrameLayout& frame, analysis::LexicalSlot slot);
}  // namespace abcd::source
#endif
