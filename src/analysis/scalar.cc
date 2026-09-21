#include "abcd/analysis/scalar.h"

#include <bit>
#include <initializer_list>
#include <optional>

namespace abcd::analysis {
namespace {
using abc::DecodedInstruction;
using abc::Opcode;
using abc::OperandKind;

std::optional<BinaryOperator> BinaryOp(Opcode opcode) {
  switch (opcode) {
    case Opcode::kADD2_IMM8_V8:
      return BinaryOperator::kAdd;
    case Opcode::kSUB2_IMM8_V8:
      return BinaryOperator::kSubtract;
    case Opcode::kMUL2_IMM8_V8:
      return BinaryOperator::kMultiply;
    case Opcode::kDIV2_IMM8_V8:
      return BinaryOperator::kDivide;
    case Opcode::kMOD2_IMM8_V8:
      return BinaryOperator::kRemainder;
    case Opcode::kEXP_IMM8_V8:
      return BinaryOperator::kExponent;
    case Opcode::kEQ_IMM8_V8:
      return BinaryOperator::kEqual;
    case Opcode::kNOTEQ_IMM8_V8:
      return BinaryOperator::kNotEqual;
    case Opcode::kLESS_IMM8_V8:
      return BinaryOperator::kLess;
    case Opcode::kLESSEQ_IMM8_V8:
      return BinaryOperator::kLessEqual;
    case Opcode::kGREATER_IMM8_V8:
      return BinaryOperator::kGreater;
    case Opcode::kGREATEREQ_IMM8_V8:
      return BinaryOperator::kGreaterEqual;
    case Opcode::kSHL2_IMM8_V8:
      return BinaryOperator::kShiftLeft;
    case Opcode::kSHR2_IMM8_V8:
      return BinaryOperator::kShiftRightUnsigned;
    case Opcode::kASHR2_IMM8_V8:
      return BinaryOperator::kShiftRightSigned;
    case Opcode::kAND2_IMM8_V8:
      return BinaryOperator::kBitAnd;
    case Opcode::kOR2_IMM8_V8:
      return BinaryOperator::kBitOr;
    case Opcode::kXOR2_IMM8_V8:
      return BinaryOperator::kBitXor;
    case Opcode::kSTRICTEQ_IMM8_V8:
      return BinaryOperator::kStrictEqual;
    case Opcode::kSTRICTNOTEQ_IMM8_V8:
      return BinaryOperator::kStrictNotEqual;
    default:
      return std::nullopt;
  }
}
enum class UnaryInput { kAccumulator, kCachedAccumulator, kRegister };
struct UnarySpec {
  UnaryOperator op;
  UnaryInput input;
};
std::optional<UnarySpec> UnaryOp(Opcode opcode) {
  // DEPRECATED_NEG is intentionally absent: the pinned C++ handler negates
  // INT32_MIN without the modern overflow path. Require a runtime-specific
  // proof before treating it as the language-level unary minus operation.
  using enum UnaryInput;
  switch (opcode) {
    case Opcode::kINC_IMM8:
      return UnarySpec{UnaryOperator::kIncrement, kCachedAccumulator};
    case Opcode::kDEC_IMM8:
      return UnarySpec{UnaryOperator::kDecrement, kCachedAccumulator};
    case Opcode::kDEPRECATED_INC_PREF_V8:
      return UnarySpec{UnaryOperator::kIncrement, kRegister};
    case Opcode::kDEPRECATED_DEC_PREF_V8:
      return UnarySpec{UnaryOperator::kDecrement, kRegister};
    case Opcode::kNEG_IMM8:
      return UnarySpec{UnaryOperator::kNegate, kCachedAccumulator};
    case Opcode::kNOT_IMM8:
      return UnarySpec{UnaryOperator::kBitNot, kCachedAccumulator};
    case Opcode::kDEPRECATED_NOT_PREF_V8:
      return UnarySpec{UnaryOperator::kBitNot, kRegister};
    case Opcode::kISTRUE:
      return UnarySpec{UnaryOperator::kToBoolean, kAccumulator};
    case Opcode::kISFALSE:
      return UnarySpec{UnaryOperator::kLogicalNot, kAccumulator};
    case Opcode::kCALLRUNTIME_ISTRUE_PREF_IMM8:
      return UnarySpec{UnaryOperator::kToBoolean, kCachedAccumulator};
    case Opcode::kCALLRUNTIME_ISFALSE_PREF_IMM8:
      return UnarySpec{UnaryOperator::kLogicalNot, kCachedAccumulator};
    case Opcode::kTYPEOF_IMM8:
    case Opcode::kTYPEOF_IMM16:
      return UnarySpec{UnaryOperator::kTypeOf, kCachedAccumulator};
    case Opcode::kTONUMBER_IMM8:
      return UnarySpec{UnaryOperator::kToNumber, kCachedAccumulator};
    case Opcode::kDEPRECATED_TONUMBER_PREF_V8:
      return UnarySpec{UnaryOperator::kToNumber, kRegister};
    default:
      return std::nullopt;
  }
}

std::optional<ScalarError> CheckOperands(
    const DecodedInstruction& instruction,
    std::initializer_list<OperandKind> kinds, std::uint64_t frame_size) {
  if (instruction.operand_count != kinds.size())
    return ScalarError::kMalformedInstruction;
  std::size_t index = 0;
  for (const auto kind : kinds) {
    const auto& operand = instruction.operands[index++];
    if (operand.kind != kind) return ScalarError::kMalformedInstruction;
    if (kind == OperandKind::kRegister &&
        (operand.raw_bits > UINT32_MAX || operand.raw_bits >= frame_size))
      return ScalarError::kRegisterRange;
  }
  return std::nullopt;
}
abc::RegisterId Register(const DecodedInstruction& instruction,
                         std::size_t index) {
  return {static_cast<std::uint32_t>(instruction.operands[index].raw_bits)};
}
ScalarAssignment AssignAccumulator(ScalarValue value) {
  return {Accumulator{}, value, CompletionEffect::kNormal};
}
}  // namespace

ScalarResult LiftScalarInstruction(const DecodedInstruction& instruction,
                                   std::uint64_t frame_size) {
  if (instruction.operand_count > instruction.operands.size())
    return ScalarError::kMalformedInstruction;
  constexpr std::size_t kFirstOperand = 0;
  constexpr std::size_t kBinaryRegister = 1;
  if (const auto op = BinaryOp(instruction.opcode)) {
    if (const auto error = CheckOperands(
            instruction, {OperandKind::kImmediate, OperandKind::kRegister},
            frame_size))
      return *error;
    // Pinned interpreter reads v as LEFT and acc as RIGHT. Preserve this even
    // for commutative operators: user coercion hooks make order observable.
    const auto completion = *op == BinaryOperator::kStrictEqual ||
                                    *op == BinaryOperator::kStrictNotEqual
                                ? CompletionEffect::kNormal
                                : CompletionEffect::kMayThrow;
    return ScalarAssignment{
        Accumulator{},
        BinaryExpression{*op, Register(instruction, kBinaryRegister),
                         Accumulator{}},
        completion};
  }
  if (const auto spec = UnaryOp(instruction.opcode)) {
    std::optional<ScalarError> error;
    ScalarValue input = Accumulator{};
    switch (spec->input) {
      case UnaryInput::kAccumulator:
        error = CheckOperands(instruction, {}, frame_size);
        break;
      case UnaryInput::kCachedAccumulator:
        error =
            CheckOperands(instruction, {OperandKind::kImmediate}, frame_size);
        break;
      case UnaryInput::kRegister:
        error =
            CheckOperands(instruction, {OperandKind::kRegister}, frame_size);
        if (!error) input = Register(instruction, kFirstOperand);
        break;
    }
    if (error) return *error;
    const auto completion = spec->op == UnaryOperator::kToBoolean ||
                                    spec->op == UnaryOperator::kLogicalNot ||
                                    spec->op == UnaryOperator::kTypeOf
                                ? CompletionEffect::kNormal
                                : CompletionEffect::kMayThrow;
    return ScalarAssignment{Accumulator{}, UnaryExpression{spec->op, input},
                            completion};
  }
  switch (instruction.opcode) {
    case Opcode::kLDOBJBYVALUE_IMM8_V8:
    case Opcode::kLDOBJBYVALUE_IMM16_V8: {
      if (const auto error = CheckOperands(
              instruction, {OperandKind::kImmediate, OperandKind::kRegister},
              frame_size))
        return *error;
      constexpr std::size_t kCacheOperand = 0;
      constexpr std::size_t kReceiverOperand = 1;
      const auto cache_max = instruction.opcode == Opcode::kLDOBJBYVALUE_IMM8_V8
                                 ? UINT8_MAX
                                 : UINT16_MAX;
      if (instruction.operands[kCacheOperand].raw_bits >
              static_cast<std::uint64_t>(cache_max) ||
          instruction.operands[kReceiverOperand].raw_bits > UINT8_MAX)
        return ScalarError::kMalformedInstruction;
      // Legacy LDOBJBYVALUE is deliberately absent: the pinned interpreter's
      // slow path reads acc instead of its encoded key register.
      return ScalarAssignment{
          Accumulator{},
          PropertyRead{Register(instruction, kReceiverOperand), Accumulator{}},
          CompletionEffect::kMayThrow};
    }
    case Opcode::kMOV_V4_V4:
    case Opcode::kMOV_V8_V8:
    case Opcode::kMOV_V16_V16: {
      if (const auto error = CheckOperands(
              instruction, {OperandKind::kRegister, OperandKind::kRegister},
              frame_size))
        return *error;
      constexpr std::size_t kMoveSource = 1;
      return ScalarAssignment{Register(instruction, kFirstOperand),
                              ScalarValue{Register(instruction, kMoveSource)},
                              CompletionEffect::kNormal};
    }
    case Opcode::kLDA_V8:
    case Opcode::kSTA_V8: {
      if (const auto error =
              CheckOperands(instruction, {OperandKind::kRegister}, frame_size))
        return *error;
      const auto reg = Register(instruction, kFirstOperand);
      if (instruction.opcode == Opcode::kLDA_V8) return AssignAccumulator(reg);
      return ScalarAssignment{reg, ScalarValue{Accumulator{}},
                              CompletionEffect::kNormal};
    }
    case Opcode::kLDAI_IMM32:
    case Opcode::kFLDAI_IMM64: {
      if (const auto error =
              CheckOperands(instruction, {OperandKind::kImmediate}, frame_size))
        return *error;
      const auto bits = instruction.operands[kFirstOperand].raw_bits;
      if (instruction.opcode == Opcode::kFLDAI_IMM64)
        return AssignAccumulator(Float64Bits{bits});
      if (bits > UINT32_MAX) return ScalarError::kMalformedInstruction;
      return AssignAccumulator(IntegerLiteral{
          std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits))});
    }
    case Opcode::kLDUNDEFINED:
    case Opcode::kLDNULL:
    case Opcode::kLDTRUE:
    case Opcode::kLDFALSE:
    case Opcode::kLDNAN:
    case Opcode::kLDINFINITY:
      break;
    default:
      return ScalarError::kUnsupportedOpcode;
  }
  if (const auto error = CheckOperands(instruction, {}, frame_size))
    return *error;
  switch (instruction.opcode) {
    case Opcode::kLDUNDEFINED:
      return AssignAccumulator(SpecialValue::kUndefined);
    case Opcode::kLDNULL:
      return AssignAccumulator(SpecialValue::kNull);
    case Opcode::kLDTRUE:
      return AssignAccumulator(SpecialValue::kTrue);
    case Opcode::kLDFALSE:
      return AssignAccumulator(SpecialValue::kFalse);
    case Opcode::kLDNAN:
      return AssignAccumulator(SpecialValue::kNaN);
    case Opcode::kLDINFINITY:
      return AssignAccumulator(SpecialValue::kInfinity);
    default:
      return ScalarError::kUnsupportedOpcode;
  }
}
}  // namespace abcd::analysis
