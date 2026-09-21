#include "abcd/analysis/branch_condition.h"

namespace abcd::analysis {
std::variant<UnaryExpression, BranchError> LiftBranchCondition(
    const abc::DecodedInstruction& instruction) {
  UnaryOperator predicate;
  using abc::Opcode;
  switch (instruction.opcode) {
    case Opcode::kJEQZ_IMM8:
    case Opcode::kJEQZ_IMM16:
    case Opcode::kJEQZ_IMM32:
      predicate = UnaryOperator::kFalseOrZero;
      break;
    case Opcode::kJNEZ_IMM8:
    case Opcode::kJNEZ_IMM16:
    case Opcode::kJNEZ_IMM32:
      predicate = UnaryOperator::kTrueOrNonzeroNumber;
      break;
    default:
      return BranchError::kUnsupportedOpcode;
  }
  if (instruction.operand_count != 1 ||
      instruction.operands[0].kind != abc::OperandKind::kImmediate ||
      !instruction.branch_delta || !instruction.jump ||
      !instruction.conditional)
    return BranchError::kMalformedInstruction;
  // interpreter-inl.cpp JEQZ/JNEZ: neither generic truthiness nor complements.
  // In particular, NaN takes JNEZ; strings, objects and BigInts take neither.
  return UnaryExpression{predicate, Accumulator{}};
}
}  // namespace abcd::analysis
