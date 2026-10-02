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
      case BinaryOperator::kInstanceOf:
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
bool HasMergedOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.class_origin || statement.previous_origin;
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

void FoldBranchExpressions(const AstMethod& method, SourceAst& ast,
                           std::span<const std::size_t> uses) {
  using analysis::UnaryOperator;
  for (const auto& block : method.flow.blocks) {
    if (block.instruction_count < 2) continue;
    const auto branch_id = std::size_t(block.first_instruction) + block.instruction_count - 1;
    auto& tail = ast.statements[branch_id];
    auto& head = ast.statements[branch_id - 1];
    auto* branch = std::get_if<SourceConditionalBreak>(&tail.operation);
    auto* loop = std::get_if<SourceConditionalContinue>(&tail.operation);
    auto* condition = branch ? &branch->condition : loop ? &loop->condition : nullptr;
    if (!condition || HasMergedOrigin(head) || HasMergedOrigin(tail) ||
        (condition->op != UnaryOperator::kLogicalNot &&
         condition->op != UnaryOperator::kToBoolean) ||
        !std::holds_alternative<analysis::Accumulator>(condition->input)) continue;
    const auto* assignment = std::get_if<SourceAssignment>(&head.operation);
    const auto* expression = assignment
        ? std::get_if<analysis::ScalarExpression>(&assignment->value) : nullptr;
    const auto* comparison = assignment
        ? std::get_if<StringComparison>(&assignment->value) : nullptr;
    if ((!expression && !comparison) || (expression && !BooleanResult(*expression)) ||
        !std::holds_alternative<analysis::Accumulator>(assignment->target)) continue;
    const auto& original = method.instructions[branch_id - 1];
    const auto* scalar = std::get_if<analysis::ScalarAssignment>(&original.operation);
    if (!scalar || !BooleanResult(scalar->expression) ||
        !std::holds_alternative<analysis::Accumulator>(scalar->destination) ||
        scalar->completion != analysis::CompletionEffect::kNormal ||
        original.effects.normal != analysis::LanguageEffect::kNone ||
        original.effects.before_throw != analysis::LanguageEffect::kNone ||
        original.effects.exceptional != analysis::ExceptionalCompletion::kImpossible)
      continue;
    const auto value = method.ssa.Write({static_cast<std::uint32_t>(branch_id - 1)});
    const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(branch_id)});
    if (!value || uses[value->value] != 1 || reads.size() != 1 ||
        reads.front().value != *value) continue;
    auto& input = branch ? branch->input : loop->input;
    if (input) continue;
    // No effect crosses the branch and no normal/exceptional observer can
    // see the removed acc commit. Both PCs share the generated condition.
    if (expression) input = *expression;
    else input = *comparison;
    head.operation = analysis::NoOperation{};
    tail.fused_read = head.pc;
    tail.read_distance = 1;
  }
}
}  // namespace abcd::source
