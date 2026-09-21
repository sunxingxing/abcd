#include "abcd/analysis/scalar.h"

#include <cstdlib>
#include <initializer_list>
#include <vector>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
abcd::abc::DecodedInstruction Decode(
    std::initializer_list<std::uint8_t> bytes) {
  const std::vector<std::uint8_t> storage(bytes);
  return std::get<abcd::abc::DecodedInstruction>(abcd::abc::DecodeInstruction(
      storage, abcd::abc::ProfileId::kDynamic1206));
}
}  // namespace
int main() {
  using namespace abcd::analysis;
  struct BinaryCase {
    std::uint8_t opcode;
    BinaryOperator op;
  };
  const BinaryCase binaries[]{{0x0a, BinaryOperator::kAdd},
                              {0x0b, BinaryOperator::kSubtract},
                              {0x0c, BinaryOperator::kMultiply},
                              {0x0d, BinaryOperator::kDivide},
                              {0x0e, BinaryOperator::kRemainder},
                              {0x0f, BinaryOperator::kEqual},
                              {0x10, BinaryOperator::kNotEqual},
                              {0x11, BinaryOperator::kLess},
                              {0x12, BinaryOperator::kLessEqual},
                              {0x13, BinaryOperator::kGreater},
                              {0x14, BinaryOperator::kGreaterEqual},
                              {0x15, BinaryOperator::kShiftLeft},
                              {0x16, BinaryOperator::kShiftRightUnsigned},
                              {0x17, BinaryOperator::kShiftRightSigned},
                              {0x18, BinaryOperator::kBitAnd},
                              {0x19, BinaryOperator::kBitOr},
                              {0x1a, BinaryOperator::kBitXor},
                              {0x1b, BinaryOperator::kExponent},
                              {0x27, BinaryOperator::kStrictNotEqual},
                              {0x28, BinaryOperator::kStrictEqual}};
  for (const auto& test : binaries) {
    const auto assignment = std::get<ScalarAssignment>(
        LiftScalarInstruction(Decode({test.opcode, 255, 7}), 8));
    const auto expression = std::get<BinaryExpression>(assignment.expression);
    Check(expression.op == test.op &&
          std::holds_alternative<Accumulator>(assignment.destination));
    Check(std::get<abcd::abc::RegisterId>(expression.left).value == 7 &&
          std::holds_alternative<Accumulator>(expression.right));
    const bool strict = test.op == BinaryOperator::kStrictEqual ||
                        test.op == BinaryOperator::kStrictNotEqual;
    Check((assignment.completion == CompletionEffect::kNormal) == strict);
  }
  struct UnaryCase {
    std::initializer_list<std::uint8_t> bytes;
    UnaryOperator op;
    bool reg;
  };
  const UnaryCase unaries[]{
      {{0x21, 99}, UnaryOperator::kIncrement, false},
      {{0x22, 99}, UnaryOperator::kDecrement, false},
      {{0xfc, 9, 7}, UnaryOperator::kIncrement, true},
      {{0xfc, 10, 7}, UnaryOperator::kDecrement, true},
      {{0x1f, 99}, UnaryOperator::kNegate, false},
      {{0x20, 99}, UnaryOperator::kBitNot, false},
      {{0xfc, 8, 7}, UnaryOperator::kBitNot, true},
      {{0x23}, UnaryOperator::kToBoolean, false},
      {{0x24}, UnaryOperator::kLogicalNot, false},
      {{0xfb, 0x13, 99}, UnaryOperator::kToBoolean, false},
      {{0xfb, 0x14, 99}, UnaryOperator::kLogicalNot, false},
      {{0x1c, 99}, UnaryOperator::kTypeOf, false},
      {{0x84, 99, 0}, UnaryOperator::kTypeOf, false},
      {{0x1d, 99}, UnaryOperator::kToNumber, false},
      {{0xfc, 5, 7}, UnaryOperator::kToNumber, true}};
  for (const auto& test : unaries) {
    const auto assignment = std::get<ScalarAssignment>(
        LiftScalarInstruction(Decode(test.bytes), 8));
    const auto expression = std::get<UnaryExpression>(assignment.expression);
    Check(expression.op == test.op);
    Check(std::holds_alternative<Accumulator>(assignment.destination));
    if (test.op == UnaryOperator::kIncrement ||
        test.op == UnaryOperator::kDecrement)
      Check(assignment.completion == CompletionEffect::kMayThrow);
    if (test.reg)
      Check(std::get<abcd::abc::RegisterId>(expression.input).value == 7);
    else
      Check(std::holds_alternative<Accumulator>(expression.input));
  }
  auto lowered = LiftScalarInstruction(Decode({0x0b, 99, 3}), 4);
  const auto sub = std::get<ScalarAssignment>(lowered);
  const auto binary = std::get<BinaryExpression>(sub.expression);
  Check(binary.op == BinaryOperator::kSubtract);
  Check(std::get<abcd::abc::RegisterId>(binary.left).value == 3);
  Check(std::holds_alternative<Accumulator>(binary.right));
  Check(sub.completion == CompletionEffect::kMayThrow);
  const auto move = std::get<ScalarAssignment>(
      LiftScalarInstruction(Decode({0x44, 0xa3}), 11));
  Check(std::get<abcd::abc::RegisterId>(move.destination).value == 3);
  Check(std::get<abcd::abc::RegisterId>(std::get<ScalarValue>(move.expression))
            .value == 10);
  for (const auto bytes :
       {std::initializer_list<std::uint8_t>{0x45, 3, 10},
        std::initializer_list<std::uint8_t>{0x8f, 3, 0, 10, 0}}) {
    const auto assignment =
        std::get<ScalarAssignment>(LiftScalarInstruction(Decode(bytes), 11));
    Check(std::get<abcd::abc::RegisterId>(assignment.destination).value == 3 &&
          std::get<abcd::abc::RegisterId>(
              std::get<ScalarValue>(assignment.expression))
                  .value == 10);
  }
  const auto load =
      std::get<ScalarAssignment>(LiftScalarInstruction(Decode({0x60, 3}), 4));
  const auto store =
      std::get<ScalarAssignment>(LiftScalarInstruction(Decode({0x61, 3}), 4));
  Check(std::holds_alternative<Accumulator>(load.destination) &&
        std::get<abcd::abc::RegisterId>(std::get<ScalarValue>(load.expression))
                .value == 3);
  Check(std::get<abcd::abc::RegisterId>(store.destination).value == 3 &&
        std::holds_alternative<Accumulator>(
            std::get<ScalarValue>(store.expression)));
  const auto integer = std::get<ScalarAssignment>(
      LiftScalarInstruction(Decode({0x62, 0, 0, 0, 0x80}), 0));
  Check(std::get<IntegerLiteral>(std::get<ScalarValue>(integer.expression))
            .value == INT32_MIN);
  const auto negative_zero = std::get<ScalarAssignment>(
      LiftScalarInstruction(Decode({0x63, 0, 0, 0, 0, 0, 0, 0, 0x80}), 0));
  Check(std::get<Float64Bits>(std::get<ScalarValue>(negative_zero.expression))
            .value == (UINT64_C(1) << 63));
  const auto nan_payload = std::get<ScalarAssignment>(LiftScalarInstruction(
      Decode({0x63, 0x42, 0, 0, 0, 0, 0, 0xf8, 0x7f}), 0));
  Check(std::get<Float64Bits>(std::get<ScalarValue>(nan_payload.expression))
            .value == UINT64_C(0x7ff8000000000042));
  Check(std::get<ScalarError>(LiftScalarInstruction(Decode({0x70}), 0)) ==
        ScalarError::kUnsupportedOpcode);  // VM hole is not JS undefined.
  Check(std::get<ScalarError>(LiftScalarInstruction(Decode({0x0b, 0, 3}), 3)) ==
        ScalarError::kRegisterRange);
  Check(std::get<ScalarError>(LiftScalarInstruction(Decode({0xfc, 7, 0}), 1)) ==
        ScalarError::kUnsupportedOpcode);  // Legacy INT_MIN path needs a
                                           // runtime profile.
  auto malformed = Decode({0x0a, 0, 0});
  malformed.operand_count = 1;
  Check(std::get<ScalarError>(LiftScalarInstruction(malformed, 1)) ==
        ScalarError::kMalformedInstruction);
  malformed = Decode({0x0a, 0, 0});
  malformed.operands[1].kind = abcd::abc::OperandKind::kImmediate;
  Check(std::get<ScalarError>(LiftScalarInstruction(malformed, 1)) ==
        ScalarError::kMalformedInstruction);
  malformed = Decode({0x0a, 0, 0});
  malformed.operands[1].raw_bits = UINT64_MAX;
  Check(std::get<ScalarError>(LiftScalarInstruction(malformed, UINT64_MAX)) ==
        ScalarError::kRegisterRange);
  malformed.operand_count = UINT8_MAX;
  Check(std::get<ScalarError>(LiftScalarInstruction(malformed, 1)) ==
        ScalarError::kMalformedInstruction);
  struct ConstantCase {
    std::uint8_t opcode;
    SpecialValue value;
  };
  for (const auto test : {ConstantCase{0, SpecialValue::kUndefined},
                          ConstantCase{1, SpecialValue::kNull},
                          ConstantCase{2, SpecialValue::kTrue},
                          ConstantCase{3, SpecialValue::kFalse},
                          ConstantCase{0x6a, SpecialValue::kNaN},
                          ConstantCase{0x6b, SpecialValue::kInfinity}}) {
    const auto assignment = std::get<ScalarAssignment>(
        LiftScalarInstruction(Decode({test.opcode}), 0));
    Check(std::get<SpecialValue>(
              std::get<ScalarValue>(assignment.expression)) == test.value);
  }
}
