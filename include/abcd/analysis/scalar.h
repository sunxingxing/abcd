#ifndef ABCD_ANALYSIS_SCALAR_H_
#define ABCD_ANALYSIS_SCALAR_H_

#include "abcd/abc/register_access.h"

namespace abcd::analysis {
struct Accumulator {};
enum class SpecialValue { kUndefined, kNull, kTrue, kFalse, kNaN, kInfinity };
struct IntegerLiteral {
  std::int32_t value;
};
struct Float64Bits {
  std::uint64_t value;
};
using ScalarSlot = std::variant<Accumulator, abc::RegisterId>;
using ScalarValue = std::variant<Accumulator, abc::RegisterId, SpecialValue,
                                 IntegerLiteral, Float64Bits>;
enum class BinaryOperator {
  kAdd,
  kSubtract,
  kMultiply,
  kDivide,
  kRemainder,
  kExponent,
  kEqual,
  kNotEqual,
  kLess,
  kLessEqual,
  kGreater,
  kGreaterEqual,
  kShiftLeft,
  kShiftRightUnsigned,
  kShiftRightSigned,
  kBitAnd,
  kBitOr,
  kBitXor,
  kStrictEqual,
  kStrictNotEqual,
  kIn
};
enum class UnaryOperator {
  kNegate,
  kBitNot,
  kToBoolean,
  kLogicalNot,
  kTypeOf,
  kToNumber,
  kToNumeric,
  kIncrement,
  kDecrement,
  kFalseOrZero,
  kTrueOrNonzeroNumber
};
struct UnaryExpression {
  UnaryOperator op;
  ScalarValue input;
};
struct BinaryExpression {
  BinaryOperator op;
  ScalarValue left;
  ScalarValue right;
};
// Computed ordinary property access, not an own-property lookup or a pure
// expression: key coercion, proxies and getters can execute arbitrary code.
// Produces a value, not a call receiver/reference. Fusing a later call must
// preserve that call's explicit this argument, not infer it from this node.
struct PropertyRead {
  ScalarValue receiver;
  ScalarValue key;
};
enum class CompletionEffect { kNormal, kMayThrow };
using ScalarExpression =
    std::variant<ScalarValue, UnaryExpression, BinaryExpression, PropertyRead>;
struct ScalarAssignment {
  ScalarSlot destination;
  ScalarExpression expression;
  CompletionEffect completion = CompletionEffect::kNormal;
};
enum class ScalarError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};
using ScalarResult = std::variant<ScalarAssignment, ScalarError>;

// A pre-SSA, single-instruction semantic boundary for pinned dynamic
// operations. Slots denote already evaluated values, not source expressions to
// duplicate. A throwing RHS does not publish the destination. VM
// hole/environment values require separate lowering and must never be
// substituted with JS undefined. Requires DecodeInstruction output. No
// allocation, coercion, or constant fold.
[[nodiscard]] ScalarResult LiftScalarInstruction(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif
