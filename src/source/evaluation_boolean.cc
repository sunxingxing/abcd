#include "evaluation_plan.h"

namespace abcd::source {
namespace {
bool Boolean(analysis::UnaryOperator op) {
  using U = analysis::UnaryOperator;
  switch (op) {
    case U::kToBoolean: case U::kLogicalNot: case U::kFalseOrZero:
    case U::kTrueOrNonzeroNumber: return true;
    case U::kNegate: case U::kBitNot: case U::kTypeOf: case U::kToNumber:
    case U::kToNumeric: case U::kIncrement: case U::kDecrement: return false;
  }
  return false;
}
bool Boolean(analysis::BinaryOperator op) {
  using B = analysis::BinaryOperator;
  switch (op) {
    case B::kEqual: case B::kNotEqual: case B::kLess: case B::kLessEqual:
    case B::kGreater: case B::kGreaterEqual: case B::kStrictEqual:
    case B::kStrictNotEqual: case B::kIn: return true;
    case B::kAdd: case B::kSubtract: case B::kMultiply: case B::kDivide:
    case B::kRemainder: case B::kExponent: case B::kShiftLeft:
    case B::kShiftRightUnsigned: case B::kShiftRightSigned:
    case B::kBitAnd: case B::kBitOr: case B::kBitXor: return false;
  }
  return false;
}
bool IsBooleanLiteral(const analysis::ValueOperand& operand) {
  const auto* value = std::get_if<analysis::SpecialValue>(&operand);
  return value && (*value == analysis::SpecialValue::kTrue || *value == analysis::SpecialValue::kFalse);
}
}  // namespace

bool BooleanEvaluation(const analysis::ValueProgram& graph, const analysis::ValueOperand& operand) {
  if (IsBooleanLiteral(operand)) return true;
  const auto* value = std::get_if<analysis::ValueId>(&operand);
  if (!value || value->value >= graph.values.size()) return false;
  const auto* result = std::get_if<analysis::OpResult>(&graph.values[value->value].origin);
  if (!result || result->op.value >= graph.ops.size()) return false;
  const auto& op = graph.ops[result->op.value];
  if (const auto* unary = std::get_if<analysis::ValueUnary>(&op.operation)) return Boolean(unary->op);
  if (const auto* binary = std::get_if<analysis::ValueBinary>(&op.operation)) return Boolean(binary->op);
  return std::holds_alternative<analysis::ValueCopy>(op.operation) && op.use_count == 1 &&
      op.first_use.value < graph.uses.size() && IsBooleanLiteral(graph.uses[op.first_use.value].operand);
}

bool PredicateSnapshot(const analysis::ValueProgram& graph,
    const analysis::ValueOp& consumer, const analysis::ValueOperand& operand) {
  std::optional<analysis::UnaryOperator> predicate;
  if (const auto* branch = std::get_if<analysis::ValueBranch>(&consumer.operation)) predicate = branch->predicate;
  if (const auto* unary = std::get_if<analysis::ValueUnary>(&consumer.operation)) predicate = unary->op;
  // These VM predicates expand to repeated JS reads unless the operand is
  // already Boolean. Never duplicate an arbitrary expression/getter here.
  return (predicate == analysis::UnaryOperator::kFalseOrZero ||
          predicate == analysis::UnaryOperator::kTrueOrNonzeroNumber) &&
         !BooleanEvaluation(graph, operand);
}
}  // namespace abcd::source
