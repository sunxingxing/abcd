#include "branch_predicate.h"

#include "copy_origins.h"

namespace abcd::source {
namespace {
bool BooleanResult(const analysis::ScalarExpression& expression) {
  using analysis::BinaryOperator;
  using analysis::UnaryOperator;
  if (const auto* scalar = std::get_if<analysis::ScalarValue>(&expression)) {
    const auto* value = std::get_if<analysis::SpecialValue>(scalar);
    return value && (*value == analysis::SpecialValue::kTrue ||
                     *value == analysis::SpecialValue::kFalse);
  }
  if (const auto* unary = std::get_if<analysis::UnaryExpression>(&expression)) {
    switch (unary->op) {
      case UnaryOperator::kToBoolean:
      case UnaryOperator::kLogicalNot:
      case UnaryOperator::kFalseOrZero:
      case UnaryOperator::kTrueOrNonzeroNumber:
        return true;
      case UnaryOperator::kNegate:
      case UnaryOperator::kBitNot:
      case UnaryOperator::kTypeOf:
      case UnaryOperator::kToNumber:
      case UnaryOperator::kToNumeric:
      case UnaryOperator::kIncrement:
      case UnaryOperator::kDecrement:
        return false;
    }
  }
  if (const auto* binary = std::get_if<analysis::BinaryExpression>(&expression)) {
    switch (binary->op) {
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
  }
  return false;
}
}  // namespace

void RefineBranchPredicates(const AstMethod& method, SourceAst& ast,
                            const CopyOrigins& copies) {
  using analysis::UnaryOperator;
  for (std::size_t i = 0; i < ast.statements.size(); ++i) {
    auto& operation = ast.statements[i].operation;
    auto* branch = std::get_if<SourceConditionalBreak>(&operation);
    auto* loop = std::get_if<SourceConditionalContinue>(&operation);
    auto* condition = branch ? &branch->condition : loop ? &loop->condition : nullptr;
    if (!condition || (condition->op != UnaryOperator::kFalseOrZero &&
                       condition->op != UnaryOperator::kTrueOrNonzeroNumber)) continue;
    const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(i)});
    if (reads.size() != 1) continue;
    const auto definition = method.ssa.Definition(copies.At(reads.front().value));
    if (!definition) continue;
    const auto* scalar = std::get_if<analysis::ScalarAssignment>(
        &method.instructions[definition->value].operation);
    if (!scalar || !BooleanResult(scalar->expression)) continue;
    // A normal SSA definition proves only its normal result. Throwing
    // comparisons still execute at the original PC; neither their inputs nor
    // effects move into the branch. Entry, catch and phi values stay unknown.
    condition->op = condition->op == UnaryOperator::kFalseOrZero
                        ? UnaryOperator::kLogicalNot : UnaryOperator::kToBoolean;
  }
}
}  // namespace abcd::source
