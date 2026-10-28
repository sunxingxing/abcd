#ifndef ABCD_ANALYSIS_VALUE_EDITS_H_
#define ABCD_ANALYSIS_VALUE_EDITS_H_

#include "abcd/analysis/value_ir.h"
#include <span>

namespace abcd::analysis {
// Internal candidate-graph primitives. Caller owns a structurally verified
// candidate and discards it on failure; never mutate a published proof's graph.
// Replacement availability/dominance is a batch verifier obligation.
enum class ValueEditError : std::uint8_t {
  kInvalidUse, kInvalidValue, kInvalidOp, kObservable, kLiveResult,
  kInvalidOrigin
};
[[nodiscard]] std::optional<ValueEditError> ReplaceUse(
    ValueProgram& candidate, UseId use, ValueId replacement);
// Literal substitution unlinks the occurrence without inventing a ValueId.
// The caller's batch proof must establish the exact primitive, including bits.
[[nodiscard]] std::optional<ValueEditError> ReplaceLiteralUse(
    ValueProgram& candidate, UseId use, ValueOperand literal);
[[nodiscard]] std::optional<ValueEditError> ErasePureOp(
    ValueProgram& candidate, OpId op);
[[nodiscard]] bool CanErasePureOp(const ValueOp& op);
// Read-only identity through pure register copies: follow live, effect-free
// single-operand ValueCopy producers to the first non-copy value. Phased graphs
// keep these copies (their import correspondence forbids edits); simplified
// normal graphs have none, so this is the identity there. `steps` receives the
// chain length for the caller's work budget; a malformed graph stops early.
[[nodiscard]] ValueId CopyRoot(const ValueProgram& graph, ValueId value, std::uint64_t& steps);
// Retention sets must be closed under every live operand dependency. Checks
// this before deleting dead cycles; no transient state escapes the candidate.
[[nodiscard]] std::optional<ValueEditError> EraseUnmarkedValues(
    ValueProgram& candidate, std::span<const std::uint8_t> live_ops,
    std::span<const std::uint8_t> live_values);
}  // namespace abcd::analysis
#endif
