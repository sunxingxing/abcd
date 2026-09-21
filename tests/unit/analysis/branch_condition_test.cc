#include "abcd/analysis/branch_condition.h"

#include <array>
#include <cstdlib>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
}  // namespace
int main() {
  using namespace abcd;
  struct Case {
    abc::Opcode opcode;
    std::size_t size;
    analysis::UnaryOperator predicate;
  };
  using enum analysis::UnaryOperator;
  const Case cases[]{{abc::Opcode::kJEQZ_IMM8, 2, kFalseOrZero},
                     {abc::Opcode::kJEQZ_IMM16, 3, kFalseOrZero},
                     {abc::Opcode::kJEQZ_IMM32, 5, kFalseOrZero},
                     {abc::Opcode::kJNEZ_IMM8, 2, kTrueOrNonzeroNumber},
                     {abc::Opcode::kJNEZ_IMM16, 3, kTrueOrNonzeroNumber},
                     {abc::Opcode::kJNEZ_IMM32, 5, kTrueOrNonzeroNumber}};
  for (const auto& item : cases) {
    std::array<std::uint8_t, 5> bytes{};
    bytes[0] = static_cast<std::uint8_t>(item.opcode);
    auto instruction = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(
        std::span(bytes).first(item.size), abc::ProfileId::kDynamic1206));
    const auto result = std::get<analysis::UnaryExpression>(
        analysis::LiftBranchCondition(instruction));
    Check(result.op == item.predicate);
    Check(std::holds_alternative<analysis::Accumulator>(result.input));
    instruction.operand_count = 0;
    Check(std::get<analysis::BranchError>(analysis::LiftBranchCondition(
              instruction)) == analysis::BranchError::kMalformedInstruction);
  }
  abc::DecodedInstruction unsupported;
  unsupported.opcode = abc::Opcode::kJSTRICTEQZ_IMM8;
  Check(std::get<analysis::BranchError>(analysis::LiftBranchCondition(
            unsupported)) == analysis::BranchError::kUnsupportedOpcode);
}
