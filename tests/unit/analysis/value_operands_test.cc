#include "value_operands.h"

#include <cstdlib>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
}

int main() {
  using namespace abcd;
  using namespace abcd::analysis;
  SemanticInstruction write{{0}, NamedWrite{{0}, {3}}, {}};
  auto& effects = write.effects.normal_registers;
  effects.read_count = 2;
  effects.reads[0] = abc::RegisterId{3};
  effects.reads[1] = Accumulator{};
  const auto plan = std::get<ValueOperands>(DescribeValueOperands(write));
  Check(plan.count == 2 && !plan.write);
  Check(plan.operands[0].role == OperandRole::kBase);
  Check(plan.operands[1].role == OperandRole::kStoredValue);
  Check(plan.operands[0].read_ordinal == 0);
  Check(plan.operands[1].read_ordinal == 1);
  std::swap(effects.reads[0], effects.reads[1]);
  Check(std::get<OperandError>(DescribeValueOperands(write)) ==
        OperandError::kReadSlot);
  effects.read_count = 0;
  Check(std::get<OperandError>(DescribeValueOperands(write)) ==
        OperandError::kReadCount);

  // Two equal register operands remain two occurrences, not one set member.
  SemanticInstruction compare{{0}, ScalarAssignment{Accumulator{},
      BinaryExpression{BinaryOperator::kStrictEqual, abc::RegisterId{3},
                       abc::RegisterId{3}}}, {}};
  auto& compare_effects = compare.effects.normal_registers;
  compare_effects.read_count = 2;
  compare_effects.reads[0] = abc::RegisterId{3};
  compare_effects.reads[1] = abc::RegisterId{3};
  compare_effects.write = Accumulator{};
  const auto repeated = std::get<ValueOperands>(DescribeValueOperands(compare));
  Check(repeated.count == 2);
  Check(repeated.operands[0].read_ordinal == 0);
  Check(repeated.operands[1].read_ordinal == 1);
  compare_effects.write = abc::RegisterId{3};
  Check(std::get<OperandError>(DescribeValueOperands(compare)) ==
        OperandError::kWriteSlot);

  SemanticInstruction guard{{0}, HoleGuard{Accumulator{}, abc::RegisterId{3}}, {}};
  guard.effects.normal_registers.read_count = 2;
  guard.effects.normal_registers.reads[0] = Accumulator{};
  guard.effects.normal_registers.reads[1] = abc::RegisterId{3};
  const auto guarded = std::get<ValueOperands>(DescribeValueOperands(guard));
  Check(guarded.operands[1].purpose == ReadPurpose::kProtocol);
  SemanticInstruction global{{0}, GlobalObjectLoad{}, {}};
  Check(std::get<OperandError>(DescribeValueOperands(global)) ==
        OperandError::kWriteSlot);
  global.effects.normal_registers.write = Accumulator{};
  const auto global_plan = std::get<ValueOperands>(DescribeValueOperands(global));
  Check(global_plan.count == 0 && global_plan.write);
  Check(std::holds_alternative<Accumulator>(*global_plan.write));
  SemanticInstruction unsupported{{0}, AsyncEnter{}, {}};
  Check(std::get<OperandError>(DescribeValueOperands(unsupported)) ==
        OperandError::kUnsupportedOperation);
}
