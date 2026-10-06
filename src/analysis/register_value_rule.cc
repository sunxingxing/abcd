#include "register_value_rule.h"

#include <algorithm>
#include <bit>
#include <cmath>

#include "abcd/analysis/array_creation.h"
#include "abcd/analysis/constant_object.h"
#include "abcd/analysis/constructor_call.h"
#include "abcd/analysis/class_creation.h"
#include "abcd/analysis/define_instruction.h"
#include "abcd/analysis/derived_constructor.h"
#include "abcd/analysis/function_creation.h"
#include "abcd/analysis/global_access.h"
#include "abcd/analysis/hole_guard.h"
#include "abcd/analysis/indexed_read.h"
#include "abcd/analysis/iter_result.h"
#include "abcd/analysis/lexical.h"
#include "abcd/analysis/named_invocation.h"
#include "abcd/analysis/named_read.h"
#include "abcd/analysis/object_creation.h"
#include "abcd/analysis/rest_arguments.h"

namespace abcd::analysis::detail {
namespace {
bool HasLanguageResult(UnaryOperator op) {
  switch (op) {
    case UnaryOperator::kToBoolean:
    case UnaryOperator::kLogicalNot:
    case UnaryOperator::kTypeOf:
    case UnaryOperator::kBitNot:
    case UnaryOperator::kFalseOrZero:
    case UnaryOperator::kTrueOrNonzeroNumber:
      return true;
    case UnaryOperator::kLegacyNegate:
    case UnaryOperator::kNegate:
    case UnaryOperator::kToNumber:
    case UnaryOperator::kToNumeric:
    case UnaryOperator::kIncrement:
    case UnaryOperator::kDecrement:
      return false;
  }
  return false;
}
bool HasLanguageResult(BinaryOperator op) {
  switch (op) {
    case BinaryOperator::kEqual:
    case BinaryOperator::kNotEqual:
    case BinaryOperator::kLess:
    case BinaryOperator::kLessEqual:
    case BinaryOperator::kGreater:
    case BinaryOperator::kGreaterEqual:
    case BinaryOperator::kStrictEqual:
    case BinaryOperator::kStrictNotEqual:
    case BinaryOperator::kInstanceOf:
    case BinaryOperator::kIn:
    // Pinned v7 FastMod/RuntimeMod2 canonicalize NaN before construction;
    // finite fmod and BigInt remainder also return language values.
    case BinaryOperator::kRemainder:
    // Pinned v7 bitwise/shift stubs return tagged int32/uint32 or BigInt on
    // normal completion. Numeric conversion failures take the abrupt path.
    case BinaryOperator::kShiftLeft:
    case BinaryOperator::kShiftRightUnsigned:
    case BinaryOperator::kShiftRightSigned:
    case BinaryOperator::kBitAnd:
    case BinaryOperator::kBitOr:
    case BinaryOperator::kBitXor:
      return true;
    case BinaryOperator::kAdd:
    case BinaryOperator::kSubtract:
    case BinaryOperator::kMultiply:
    case BinaryOperator::kDivide:
    case BinaryOperator::kExponent:
      return false;
  }
  return false;
}
}  // namespace
ValueKinds FloatNumberKinds(Float64Bits number) {
  // Pinned JSTaggedValue::IsImpureNaN compares raw bits against TAG_INT minus
  // DOUBLE_ENCODE_OFFSET. Share this boundary with source numeric identities;
  // never classify a VM-private encoding as Number merely because it is FLDAI.
  constexpr std::uint64_t kImpureNaNBitsMin = UINT64_C(0xfffe000000000000);
  if (number.value >= kImpureNaNBitsMin) return ValueKinds::Unknown();
  constexpr std::uint64_t kMagnitudeMask = UINT64_C(0x7fffffffffffffff);
  constexpr std::uint64_t kInfinityBits = UINT64_C(0x7ff0000000000000);
  const auto magnitude = number.value & kMagnitudeMask;
  if (magnitude > kInfinityBits) return ValueKinds::Number();
  auto kinds = (magnitude != 0 && magnitude < kInfinityBits ? ValueKinds::FiniteNonzeroNumber() :
      magnitude == 0 ? ValueKinds::FiniteNumber() : ValueKinds::NonNaNNumber()).WithoutInt32MinTag();
  if (kinds.IsFiniteNumber()) {
    const auto literal = std::bit_cast<double>(number.value);
    if (std::trunc(literal) == literal) kinds = kinds.WithIntegralNumber();
  }
  return kinds;
}
ValueRule DescribeScalarValue(const ScalarAssignment& assignment, ArithmeticSemantics arithmetic) {
  if (const auto* value = std::get_if<ScalarValue>(&assignment.expression)) {
    if (std::holds_alternative<Accumulator>(*value) ||
        std::holds_alternative<abc::RegisterId>(*value))
      return ValueRule{ValueRuleKind::kJoin, {}};
    if (const auto* number = std::get_if<Float64Bits>(value))
      return ValueRule{ValueRuleKind::kFixed, FloatNumberKinds(*number)};
    if (const auto* integer = std::get_if<IntegerLiteral>(value)) {
      const auto kinds = (integer->value != 0 ? ValueKinds::FiniteNonzeroNumber() : ValueKinds::FiniteNumber()).WithIntegralNumber();
      return ValueRule{ValueRuleKind::kFixed, integer->value == INT32_MIN ? kinds : kinds.WithoutInt32MinTag()};
    }
    if (const auto* special = std::get_if<SpecialValue>(value)) {
      if (*special == SpecialValue::kTrue || *special == SpecialValue::kFalse || *special == SpecialValue::kNull)
        return ValueRule{ValueRuleKind::kFixed, ValueKinds::ZeroOrOnePrimitive()};
      if (*special == SpecialValue::kNaN) return ValueRule{ValueRuleKind::kFixed, ValueKinds::Number()};
      if (*special == SpecialValue::kInfinity)
        return ValueRule{ValueRuleKind::kFixed, ValueKinds::NonNaNNumber().WithoutInt32MinTag()};
    }
    return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
  }
  // Successful ECMAScript property reads expose language values. The pinned
  // v7.0 JSObject::GetProperty returns undefined for a missing data property;
  // Hole/environment sentinels are not observable through this API. Receiver
  // and key reads remain checked by RegisterEffect, and abrupt getters still
  // preserve the normal/throwing control semantics of the instruction.
  if (std::holds_alternative<PropertyRead>(assignment.expression))
    return ValueRule{ValueRuleKind::kLanguageOperation, {}};
  // Language includes accepted noncanonical NaNs. NEG can flip such a NaN
  // into an impure encoding; numeric results need stronger numeric facts.
  bool language = false;
  if (const auto* unary =
          std::get_if<UnaryExpression>(&assignment.expression)) {
    // Pinned v7 interpreter-inl.cpp: NEG/INC/DEC keep a non-NaN Number
    // non-NaN (including infinities and int32 overflow to double), while
    // TONUMBER/TONUMERIC return numeric input unchanged. Preserve this fact
    // through loop-carried SSA values; never extend it to arbitrary Language
    // inputs, whose coercions or NaN payloads need separate proofs.
    switch (unary->op) {
      case UnaryOperator::kNegate:
        return ValueRule{ValueRuleKind::kNegate, {}, arithmetic};
      case UnaryOperator::kLegacyNegate:
        return ValueRule{ValueRuleKind::kLegacyNegate, {}, arithmetic};
      case UnaryOperator::kBitNot:
        return ValueRule{ValueRuleKind::kBitNot, {}};
      case UnaryOperator::kIncrement:
      case UnaryOperator::kDecrement:
        return ValueRule{ValueRuleKind::kNumericUpdate, {}, arithmetic};
      case UnaryOperator::kToNumber:
        return ValueRule{ValueRuleKind::kToNumber, {}};
      case UnaryOperator::kToNumeric:
        return ValueRule{ValueRuleKind::kToNumeric, {}};
      case UnaryOperator::kToBoolean:
      case UnaryOperator::kLogicalNot:
      case UnaryOperator::kFalseOrZero:
      case UnaryOperator::kTrueOrNonzeroNumber:
        return ValueRule{ValueRuleKind::kLanguageOperation, ValueKinds::ZeroOrOnePrimitive()};
      case UnaryOperator::kTypeOf:
        return ValueRule{ValueRuleKind::kLanguageOperation, ValueKinds::String()};
    }
    language = HasLanguageResult(unary->op);
  }
  if (const auto* binary =
          std::get_if<BinaryExpression>(&assignment.expression)) {
    // Pinned interpreter FastEqual/FastStrictEqual and slow equality return
    // tagged booleans on normal completion, including the compiler's Hole
    // comparisons. This does not make their operands language values or
    // remove coercion/exception effects from the operation.
    if (binary->op == BinaryOperator::kEqual || binary->op == BinaryOperator::kNotEqual ||
        binary->op == BinaryOperator::kStrictEqual || binary->op == BinaryOperator::kStrictNotEqual)
      return ValueRule{ValueRuleKind::kEquality, ValueKinds::ZeroOrOnePrimitive()};
    if (binary->op == BinaryOperator::kLess || binary->op == BinaryOperator::kLessEqual ||
        binary->op == BinaryOperator::kGreater || binary->op == BinaryOperator::kGreaterEqual ||
        binary->op == BinaryOperator::kInstanceOf || binary->op == BinaryOperator::kIn)
      return ValueRule{ValueRuleKind::kLanguageOperation, ValueKinds::ZeroOrOnePrimitive()};
    const auto numeric_rule = [&](ValueRuleKind kind) {
      return ValueRule{kind, {}, arithmetic};
    };
    if (binary->op == BinaryOperator::kRemainder && LanguageBinaryArithmetic(arithmetic))
      return ValueRule{ValueRuleKind::kLanguageOperation, ValueKinds::Defined()};
    if (binary->op == BinaryOperator::kAdd)
      return numeric_rule(ValueRuleKind::kAdd);
    if (binary->op == BinaryOperator::kSubtract)
      return numeric_rule(ValueRuleKind::kSubtract);
    if (binary->op == BinaryOperator::kMultiply)
      return numeric_rule(ValueRuleKind::kNonNaNMultiply);
    if (binary->op == BinaryOperator::kDivide)
      return numeric_rule(ValueRuleKind::kNonNaNDivide);
    if (binary->op == BinaryOperator::kExponent)
      return numeric_rule(ValueRuleKind::kIntegralExponent);
    if (binary->op == BinaryOperator::kShiftRightUnsigned)
      return numeric_rule(ValueRuleKind::kUnsignedShift);
    if (binary->op == BinaryOperator::kShiftLeft || binary->op == BinaryOperator::kShiftRightSigned ||
        binary->op == BinaryOperator::kBitAnd || binary->op == BinaryOperator::kBitOr || binary->op == BinaryOperator::kBitXor)
      return numeric_rule(ValueRuleKind::kBitwise);
    language = HasLanguageResult(binary->op);
  }
  return language ? ValueRule{ValueRuleKind::kLanguageOperation, {}}
                  : ValueRule{ValueRuleKind::kFixed, ValueKinds::Unknown()};
}
std::variant<ValueRule, ScalarError> DescribeRegisterValue(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size, ArithmeticSemantics arithmetic) {
  const auto effect = LiftRegisterEffect(instruction, frame_size);
  if (const auto* error = std::get_if<ScalarError>(&effect)) return *error;
  if (!std::get<RegisterEffect>(effect).write)
    return ScalarError::kMalformedInstruction;
  const auto scalar = LiftScalarInstruction(instruction, frame_size);
  if (const auto* assignment = std::get_if<ScalarAssignment>(&scalar))
    return DescribeScalarValue(*assignment, arithmetic);
  if (std::holds_alternative<LoadHole>(
          LiftHoleInstruction(instruction, frame_size)))
    return ValueRule{ValueRuleKind::kFixed, ValueKinds::Hole()};
  if (std::holds_alternative<PushEnvironment>(
          LiftLexicalInstruction(instruction, frame_size)))
    return ValueRule{ValueRuleKind::kFixed, ValueKinds::Environment()};
  if (std::holds_alternative<NamedRead>(LiftNamedRead(instruction)) ||
      std::holds_alternative<IndexedRead>(
          LiftIndexedRead(instruction, frame_size)))
    return ValueRule{ValueRuleKind::kLanguageOperation, {}};
  if (std::holds_alternative<ApplyConstructorCall>(
          LiftApplyConstructorCall(instruction, frame_size)) ||
      std::holds_alternative<ConstructorCall>(
          LiftConstructorCall(instruction, frame_size)))
    return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
  const auto super = LiftSuperInstruction(instruction,
      static_cast<std::uint32_t>(std::min<std::uint64_t>(frame_size, UINT32_MAX)));
  if (std::holds_alternative<SuperForward>(super) || std::holds_alternative<SuperRange>(super) || std::holds_alternative<SuperSpread>(super))
    return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
  if (std::holds_alternative<ReceiverCall>(
          LiftReceiverCall(instruction, frame_size)))
    return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
  const auto constant = LiftConstantObjectInstruction(instruction);
  if (const auto* value = std::get_if<ConstantObjectInstruction>(&constant)) {
    // These pinned handlers return ordinary ECMAScript objects on normal
    // completion. This is a value-kind fact, not source-emission permission.
    switch (value->kind) {
      case ConstantObjectKind::kSymbolConstructor:
      case ConstantObjectKind::kGlobalObject:
      case ConstantObjectKind::kUnmappedArguments:
      case ConstantObjectKind::kAsyncFunctionEnter:
      case ConstantObjectKind::kCurrentFunction:
        return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
      default:
        break;
    }
  }
  if (instruction.opcode == abc::Opcode::kLDA_STR_ID16)
    return ValueRule{ValueRuleKind::kFixed, ValueKinds::String()};
  if (instruction.opcode == abc::Opcode::kCREATEEMPTYARRAY_IMM8 ||
      std::holds_alternative<BufferedArrayCreation>(
          LiftArrayCreation(instruction)) ||
      std::holds_alternative<EmptyObjectCreation>(
          LiftObjectCreation(instruction)) ||
      std::holds_alternative<BufferedObjectCreation>(
          LiftObjectCreation(instruction)) ||
      std::holds_alternative<IterResult>(
          LiftIterResult(instruction, frame_size)) ||
      std::holds_alternative<RestCopy>(LiftRestArguments(instruction)) ||
      std::holds_alternative<FunctionCreation>(
          LiftFunctionCreation(instruction)) ||
      std::holds_alternative<ClassCreation>(LiftClassCreation(instruction)) ||
      std::holds_alternative<DefineAccessor>(
          LiftDefineInstruction(instruction, frame_size)))
    return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
  const auto global = LiftGlobalAccess(instruction);
  if (const auto* access = std::get_if<GlobalAccess>(&global);
      access && access->operation == GlobalOp::kReadName)
    return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
  return ValueRule{ValueRuleKind::kFixed, ValueKinds::Unknown()};
}
}  // namespace abcd::analysis::detail
