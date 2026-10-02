#ifndef ABCD_ANALYSIS_REGISTER_VALUE_RULE_H_
#define ABCD_ANALYSIS_REGISTER_VALUE_RULE_H_

#include "abcd/analysis/register_effect.h"
#include "abcd/analysis/call_result.h"
#include "abcd/analysis/frame_receiver.h"
#include "abcd/analysis/value_kinds.h"

namespace abcd::analysis {
struct SemanticInstruction;
}
namespace abcd::analysis::detail {
inline constexpr std::size_t kScalarInputs = 2;
enum class ValueRuleKind {
  kFixed,
  kJoin,
  kLanguageOperation,
  kEquality,
  kAdd,
  kSubtract,
  kNonNaNUnary,
  kNegate,
  kLegacyNegate,
  kNumericIdentity,
  kNonNaNMultiply,
  kNonNaNDivide
};
struct ValueRule {
  ValueRuleKind kind = ValueRuleKind::kFixed;
  ValueKinds seed;
};
// Pinned Add2/Sub2 use IEEE arithmetic after Number conversion. With two
// non-NaN Numbers and at least one finite operand, no infinity cancellation
// can produce NaN. Overflow is allowed; arbitrary NaN payloads remain refused.
constexpr ValueKinds AdditiveNumbers(ValueKinds left, ValueKinds right) {
  return left.IsNonNaNNumber() && right.IsNonNaNNumber() &&
      (left.IsFiniteNumber() || right.IsFiniteNumber()) ? ValueKinds::NonNaNNumber() : ValueKinds::Unknown();
}
// Pinned interpreter SUB2 negates its int32 RHS before its overflow check.
// INT32_MIN makes that C++ path undefined. Do not certify native subtraction
// without evidence that the RHS cannot carry that tag; a double is distinct.
constexpr ValueKinds SubtractNumbers(ValueKinds left, ValueKinds right) {
  return right.ExcludesInt32MinTag() ? AdditiveNumbers(left, right) : ValueKinds::Unknown();
}
constexpr ValueKinds AddValues(ValueKinds left, ValueKinds right) {
  if (!left.IsLanguage() || !right.IsLanguage()) return ValueKinds::Unknown();
  return left.IsString() || right.IsString() ? ValueKinds::String() : AdditiveNumbers(left, right);
}
// Modern NEG handles the tagged minimum via double overflow; the legacy
// integer path needs its exclusion. Both produce a non-minimum representation
// when proved, and unary sign change retains finite/nonzero information.
constexpr ValueKinds NegatedNumber(ValueKinds input) {
  if (!input.IsNonNaNNumber()) return ValueKinds::Unknown();
  return (input.IsFiniteNonzeroNumber() ? ValueKinds::FiniteNonzeroNumber() :
      input.IsFiniteNumber() ? ValueKinds::FiniteNumber() : ValueKinds::NonNaNNumber()).WithoutInt32MinTag();
}
constexpr ValueKinds LegacyNegatedNumber(ValueKinds input) {
  return input.ExcludesInt32MinTag() ? NegatedNumber(input) : ValueKinds::Unknown();
}
// IEEE Number multiplication cannot create NaN without a NaN input or a
// zero/infinity pair. Overflow and underflow may lose finite/nonzero facts.
constexpr ValueKinds MultiplyNumbers(ValueKinds left, ValueKinds right) {
  return left.IsNonNaNNumber() && right.IsNonNaNNumber() &&
                 (left.IsFiniteNonzeroNumber() || right.IsFiniteNonzeroNumber())
             ? ValueKinds::NonNaNNumber() : ValueKinds::Unknown();
}
// Pinned FastDiv handles a zero divisor before IEEE division. With no NaN
// input and at least one finite/nonzero input, neither infinity/infinity nor
// zero/zero reaches raw division. Overflow, underflow and signed zero remain
// non-NaN Numbers. This proves the normal result only, never erases effects.
constexpr ValueKinds DivideNumbers(ValueKinds left, ValueKinds right) {
  return left.IsNonNaNNumber() && right.IsNonNaNNumber() &&
                 (left.IsFiniteNonzeroNumber() || right.IsFiniteNonzeroNumber())
             ? ValueKinds::NonNaNNumber() : ValueKinds::Unknown();
}
// Classifies a definite normal-path register definition. Join is one copy;
// language operations have at most two scalar operands. Property/call/lexical
// results stay unknown absent heap/callee/cell evidence, not optimistic JS
// types.
std::variant<ValueRule, ScalarError> DescribeRegisterValue(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
ValueRule DescribeScalarValue(const ScalarAssignment& assignment);
std::variant<ValueRule, ScalarError> DescribeRegisterValue(
    const SemanticInstruction& instruction,
    CallResultValues calls = CallResultValues::kUnknown,
    FrameReceiver receiver = FrameReceiver::kUnknown);
}  // namespace abcd::analysis::detail
#endif  // ABCD_ANALYSIS_REGISTER_VALUE_RULE_H_
