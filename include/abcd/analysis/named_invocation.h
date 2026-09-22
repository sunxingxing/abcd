#ifndef ABCD_ANALYSIS_NAMED_INVOCATION_H_
#define ABCD_ANALYSIS_NAMED_INVOCATION_H_

#include "abcd/abc/method_body.h"
#include "abcd/analysis/named_read.h"
#include "abcd/analysis/ordinary_call.h"

namespace abcd::analysis {
struct ReceiverCall {
  abc::RegisterId receiver;
  CallRegisters arguments;
};
// Fixed CALLTHIS0..3: accumulator callee, explicit raw this, undefined
// new.target. Lifting alone does not permit source .call/.apply synthesis.
[[nodiscard]] std::variant<ReceiverCall, CallError> LiftReceiverCall(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
struct NamedInvocation {
  NamedRead property;
  ReceiverCall call;
  // Number of bytecode instructions consumed by the fused source call.
  // Direct read/call is two; compiler spill/reload form is four.
  std::uint8_t instruction_count = 2;
};
// Input span is ONE unchanged basic block (or validated straight-line body).
// Copy from/to the receiver; named read; call with exactly that receiver.
// Private register bindings cannot be mutated by the getter. No operation or
// control-flow entry may be crossed. O(1), fixed storage, no name-string proof.
[[nodiscard]] std::optional<NamedInvocation> MatchNamedInvocation(
    std::span<const abc::BodyInstruction> block, std::size_t property,
    std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_NAMED_INVOCATION_H_
