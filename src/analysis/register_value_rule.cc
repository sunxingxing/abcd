#include "register_value_rule.h"

#include "abcd/analysis/function_creation.h"
#include "abcd/analysis/global_access.h"
#include "abcd/analysis/hole_guard.h"
#include "abcd/analysis/indexed_read.h"
#include "abcd/analysis/lexical.h"
#include "abcd/analysis/named_read.h"
#include "abcd/analysis/object_creation.h"

namespace abcd::analysis::detail {
namespace {
bool HasLanguageResult(UnaryOperator op) {
  switch (op) {
    case UnaryOperator::kToBoolean:
    case UnaryOperator::kLogicalNot:
    case UnaryOperator::kTypeOf:
    case UnaryOperator::kFalseOrZero:
    case UnaryOperator::kTrueOrNonzeroNumber:
      return true;
    case UnaryOperator::kNegate:
    case UnaryOperator::kBitNot:
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
    case BinaryOperator::kIn:
      return true;
    case BinaryOperator::kAdd:
    case BinaryOperator::kSubtract:
    case BinaryOperator::kMultiply:
    case BinaryOperator::kDivide:
    case BinaryOperator::kRemainder:
    case BinaryOperator::kExponent:
    case BinaryOperator::kShiftLeft:
    case BinaryOperator::kShiftRightUnsigned:
    case BinaryOperator::kShiftRightSigned:
    case BinaryOperator::kBitAnd:
    case BinaryOperator::kBitOr:
    case BinaryOperator::kBitXor:
      return false;
  }
  return false;
}
}  // namespace
std::variant<ValueRule, ScalarError> DescribeRegisterValue(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  const auto effect = LiftRegisterEffect(instruction, frame_size);
  if (const auto* error = std::get_if<ScalarError>(&effect)) return *error;
  if (!std::get<RegisterEffect>(effect).write)
    return ScalarError::kMalformedInstruction;
  const auto scalar = LiftScalarInstruction(instruction, frame_size);
  if (const auto* assignment = std::get_if<ScalarAssignment>(&scalar)) {
    if (const auto* value = std::get_if<ScalarValue>(&assignment->expression)) {
      if (std::holds_alternative<Accumulator>(*value) ||
          std::holds_alternative<abc::RegisterId>(*value))
        return ValueRule{ValueRuleKind::kJoin, {}};
      if (const auto* number = std::get_if<Float64Bits>(value)) {
        // Pinned JSTaggedValue::IsImpureNaN compares raw bits with TAG_INT -
        // DOUBLE_ENCODE_OFFSET. Release construction only adds that offset;
        // raw FLDAI payloads can therefore encode Hole/other internal values.
        constexpr std::uint64_t kImpureNaNBitsMin =
            UINT64_C(0xfffe000000000000);
        if (number->value >= kImpureNaNBitsMin)
          return ValueRule{ValueRuleKind::kFixed, ValueKinds::Unknown()};
      }
      return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
    }
    // Successful ECMAScript property reads expose language values. The pinned
    // v7.0 JSObject::GetProperty returns undefined for a missing data property;
    // Hole/environment sentinels are not observable through this API. Receiver
    // and key reads remain checked by RegisterEffect, and abrupt getters still
    // preserve the normal/throwing control semantics of the instruction.
    if (std::holds_alternative<PropertyRead>(assignment->expression)) {
      return ValueRule{ValueRuleKind::kLanguageOperation, {}};
    }
    // Language includes accepted noncanonical NaNs. NEG can flip such a NaN
    // into an impure encoding; numeric results need stronger numeric facts.
    bool language = false;
    if (const auto* unary =
            std::get_if<UnaryExpression>(&assignment->expression))
      language = HasLanguageResult(unary->op);
    if (const auto* binary =
            std::get_if<BinaryExpression>(&assignment->expression))
      language = HasLanguageResult(binary->op);
    return language ? ValueRule{ValueRuleKind::kLanguageOperation, {}}
                    : ValueRule{ValueRuleKind::kFixed, ValueKinds::Unknown()};
  }
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
  if (instruction.opcode == abc::Opcode::kLDA_STR_ID16 ||
      instruction.opcode == abc::Opcode::kCREATEEMPTYARRAY_IMM8 ||
      std::holds_alternative<EmptyObjectCreation>(
          LiftObjectCreation(instruction)) ||
      std::holds_alternative<BufferedObjectCreation>(
          LiftObjectCreation(instruction)) ||
      std::holds_alternative<FunctionCreation>(
          LiftFunctionCreation(instruction)))
    return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
  const auto global = LiftGlobalAccess(instruction);
  if (const auto* access = std::get_if<GlobalAccess>(&global);
      access && access->operation == GlobalOp::kReadName)
    return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
  return ValueRule{ValueRuleKind::kFixed, ValueKinds::Unknown()};
}
}  // namespace abcd::analysis::detail
