#ifndef ABCD_ANALYSIS_NAMED_INVOCATION_H_
#define ABCD_ANALYSIS_NAMED_INVOCATION_H_

#include <vector>

#include "abcd/abc/method_body.h"
#include "abcd/analysis/named_read.h"
#include "abcd/analysis/ordinary_call.h"

namespace abcd::analysis {
struct ReceiverCall {
  abc::RegisterId receiver;
  std::variant<CallRegisters, CallRange> arguments;
  CallArgumentForm form = CallArgumentForm::kRegisters;
  ScalarSlot callee = Accumulator{};
  [[nodiscard]] std::uint32_t Count() const;
  [[nodiscard]] std::optional<abc::RegisterId> At(
      std::uint32_t position) const;
};
// Fixed CALLTHIS0..3 or narrow CALLTHISRANGE: accumulator callee, explicit
// raw this, undefined new.target. Range count excludes the receiver and
// begins at receiver + 1 (pinned v7 CALL_PUSH_ARGS_THISRANGE).
// Lifting alone does not permit source .call/.apply synthesis.
// DEPRECATED_CALLTHISRANGE (count includes the receiver) and
// DEPRECATED_CALLSPREAD (APPLY semantics) read the callee from a register;
// fused property-call matchers accept accumulator callees only.
[[nodiscard]] std::variant<ReceiverCall, CallError> LiftReceiverCall(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
struct NamedInvocation {
  NamedRead property;
  ReceiverCall call;
  // Number of bytecode instructions consumed by the fused source call.
  // Direct read/call is two; compiler spill/reload form is four.
  std::uint8_t instruction_count = 2;
};
enum class SpilledCallTiming { kImmediate, kDeferredArgument };
struct StagedNamedInvocation {
  std::size_t property;
  std::size_t spill;
  std::size_t reload;
  std::size_t call;
  NamedRead read;
  ReceiverCall invoke;
  abc::RegisterId callee;
  SpilledCallTiming timing = SpilledCallTiming::kDeferredArgument;
};
struct CopiedArgumentInvocation {
  std::size_t property;
  std::size_t spill;
  std::size_t reload;
  NamedInvocation invoke;
};
// Input span is ONE unchanged basic block (or validated straight-line body).
// Copy from/to the receiver; named read; call with exactly that receiver.
// Private register bindings cannot be mutated by the getter. No operation or
// control-flow entry may be crossed. O(1), fixed storage, no name-string proof.
[[nodiscard]] std::optional<NamedInvocation> MatchNamedInvocation(
    std::span<const abc::BodyInstruction> block, std::size_t property,
    std::uint64_t frame_size);
// One private argument copy may be commuted before a named getter only when
// the saved callee is dead at the call's next instruction. No effectful work,
// exception entry, or receiver/argument alias may be crossed. O(1).
[[nodiscard]] std::optional<CopiedArgumentInvocation>
MatchCopiedArgumentInvocation(
    std::span<const abc::BodyInstruction> block, std::size_t call,
    std::uint64_t frame_size);
// Straight-line CALLTHIS1..3 or narrow CALLTHISRANGE with at least one
// argument, whose callee is one earlier named property read.
// The first post-spill operation must overwrite acc without reading it; the
// callee register is dead after the call or overwritten before reuse, and the
// receiver register is not overwritten. Source puts intervening statements inside
// the first argument's arrow IIFE, retaining getter-before-argument order.
// O(instructions), constant extra storage; no mutable call intrinsic.
[[nodiscard]] std::optional<StagedNamedInvocation> MatchTailNamedInvocation(
    const abc::MethodBody& body, std::uint64_t frame_size);
// Compatibility probe for one receiver call in a complete straight-line body.
// Production uses the function-wide matcher below.
[[nodiscard]] std::optional<StagedNamedInvocation> MatchStagedNamedInvocation(
    const abc::MethodBody& body, std::uint64_t frame_size);
enum class StagedMatchError { kWorkLimit, kResourceLimit };
struct StagedMatches {
  std::vector<StagedNamedInvocation> calls;
  std::uint64_t work_units = 0;
};
using StagedMatchResult = std::variant<StagedMatches, StagedMatchError>;
// Function-wide spilled calls. Zero-argument immediate calls may occur inside
// a deferred call's argument evaluation; other overlaps are rejected. Timelines avoid a
// suffix/prefix walk per call; work is linear in instructions and expanded
// accesses to the 256 low v8-addressable registers. Scratch and retained
// matches share the caller's byte budget; work_units charge its method budget.
// Empty matches mean no complete proof.
[[nodiscard]] StagedMatchResult MatchStagedNamedInvocations(
    const abc::MethodBody& body, std::uint64_t frame_size,
    std::size_t bytes, std::uint64_t work_limit);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_NAMED_INVOCATION_H_
