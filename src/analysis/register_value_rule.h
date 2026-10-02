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
  kNumericUpdate,
  kNegate,
  kLegacyNegate,
  kToNumeric,
  kToNumber,
  kNonNaNMultiply,
  kNonNaNDivide,
  kIntegralExponent,
  kBitwise,
  kUnsignedShift,
  kBitNot
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
// ToNumber normal completion excludes BigInt, but may retain a NaN payload.
// ToNumeric preserves BigInt, so its general bound remains Defined Language.
constexpr ValueKinds ConvertedNumber(ValueKinds input) {
  if (!input.IsLanguage()) return ValueKinds::Unknown();
  return input.IsNumber() ? input : ValueKinds::Number();
}
constexpr ValueKinds ConvertedNumericValue(ValueKinds input) {
  if (!input.IsLanguage()) return ValueKinds::Unknown();
  return input.IsNumber() ? input : ValueKinds::Defined();
}
// Modern NEG handles the tagged minimum via double overflow; the legacy
// integer path needs its exclusion. Both produce a non-minimum representation
// when proved, and unary sign change retains finite/nonzero information.
constexpr ValueKinds NegatedNumber(ValueKinds input) {
  if (!input.IsNonNaNNumber()) return ValueKinds::Unknown();
  const auto result = (input.IsFiniteNonzeroNumber() ? ValueKinds::FiniteNonzeroNumber() :
      input.IsFiniteNumber() ? ValueKinds::FiniteNumber() : ValueKinds::NonNaNNumber()).WithoutInt32MinTag();
  return input.IsIntegralNumber() ? result.WithIntegralNumber() : result;
}
constexpr ValueKinds LegacyNegatedNumber(ValueKinds input) {
  return input.ExcludesInt32MinTag() ? NegatedNumber(input) : ValueKinds::Unknown();
}
// Pinned modern/deprecated INC/DEC guard int32 overflow and otherwise add or
// subtract one as binary64. Finite inputs stay finite: near the largest finite
// magnitude, one is below half an ULP. Integer inputs remain integral, but zero
// and tagged INT32_MIN can be produced, so neither exclusion is inherited.
constexpr ValueKinds UpdatedNumber(ValueKinds input) {
  if (!input.IsNonNaNNumber()) return ValueKinds::Unknown();
  const auto result = input.IsFiniteNumber() ? ValueKinds::FiniteNumber() : ValueKinds::NonNaNNumber();
  return input.IsIntegralNumber() ? result.WithIntegralNumber() : result;
}
// IEEE Number multiplication cannot create NaN without a NaN input or a
// zero/infinity pair. Two finite inputs exclude that pair even when both may
// be zero (e.g. two int32 coercions). Pinned FastRuntimeStub::FastMul performs
// double multiplication. Overflow and underflow lose finite/nonzero facts.
constexpr ValueKinds MultiplyNumbers(ValueKinds left, ValueKinds right) {
  return left.IsNonNaNNumber() && right.IsNonNaNNumber() &&
                 ((left.IsFiniteNumber() && right.IsFiniteNumber()) ||
                  left.IsFiniteNonzeroNumber() || right.IsFiniteNonzeroNumber())
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
// Pinned RuntimeShl2/Ashr2/And2/Or2/Xor2 convert left then right with ToNumeric.
// Mixed Number/BigInt is abrupt. One proved Number therefore makes every normal
// result int32; otherwise normal results are Number or BigInt, never undefined.
constexpr ValueKinds BitwiseValues(ValueKinds left, ValueKinds right) {
  if (!left.IsLanguage() || !right.IsLanguage()) return ValueKinds::Unknown();
  return left.IsNumber() || right.IsNumber()
      ? ValueKinds::FiniteNumber().WithIntegralNumber() : ValueKinds::Defined();
}
// Bitwise NOT also preserves BigInt; a proved Number takes ToInt32 and yields
// an integer Number. INT32_MIN remains possible, so no tag exclusion is added.
constexpr ValueKinds BitNotValue(ValueKinds input) {
  if (!input.IsLanguage()) return ValueKinds::Unknown();
  return input.IsNumber() ? ValueKinds::FiniteNumber().WithIntegralNumber() : ValueKinds::Defined();
}
// RuntimeShr2 rejects BigInt unsigned shift. uint32 normal results cannot carry
// the negative tagged INT32_MIN representation; this is not true of other shifts.
constexpr ValueKinds UnsignedShiftValues(ValueKinds left, ValueKinds right) {
  return left.IsLanguage() && right.IsLanguage()
      ? ValueKinds::FiniteNumber().WithIntegralNumber().WithoutInt32MinTag() : ValueKinds::Unknown();
}
// Pinned EXP delegates Number arithmetic to pow after its NaN/infinity guards.
// A non-NaN base with a finite integer exponent cannot enter a NaN domain:
// negative bases, signed zero, overflow/underflow and infinities stay Numbers.
// Fractional exponents and arbitrary NaN payloads require separate evidence.
constexpr ValueKinds ExponentNumbers(ValueKinds base, ValueKinds exponent) {
  return base.IsNonNaNNumber() && exponent.IsIntegralNumber()
      ? ValueKinds::NonNaNNumber() : ValueKinds::Unknown();
}
// Shared literal classification, including the pinned private-tag boundary.
[[nodiscard]] ValueKinds FloatNumberKinds(Float64Bits number);
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
