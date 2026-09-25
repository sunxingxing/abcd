#include "register_assignment.h"

namespace abcd::source {
namespace {
bool HasMergedOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.class_origin || statement.previous_origin;
}
bool IsAccumulator(const analysis::ScalarExpression& expression) {
  const auto* value = std::get_if<analysis::ScalarValue>(&expression);
  return value && std::holds_alternative<analysis::Accumulator>(*value);
}
bool NeedsAggregateFold(const SourceExpression& expression) {
  if (std::holds_alternative<ClassExpression>(expression) ||
      std::holds_alternative<analysis::BufferedArrayCreation>(expression) ||
      std::holds_alternative<analysis::BufferedObjectCreation>(expression)) return true;
  const auto* scalar = std::get_if<analysis::ScalarExpression>(&expression);
  const auto* value = scalar ? std::get_if<analysis::ScalarValue>(scalar) : nullptr;
  return value && std::holds_alternative<analysis::EmptyArray>(*value);
}
}  // namespace

void FoldRegisterAssignments(const AstMethod& method, SourceAst& ast,
                             std::span<const std::size_t> uses) {
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t i = block.first_instruction; i + 1 < end; ++i) {
      auto& head = ast.statements[i];
      auto& tail = ast.statements[i + 1];
      auto* assignment = std::get_if<SourceAssignment>(&head.operation);
      const auto* save = std::get_if<SourceAssignment>(&tail.operation);
      if (!assignment || !save || HasMergedOrigin(head) || HasMergedOrigin(tail) ||
          !std::holds_alternative<analysis::Accumulator>(assignment->target) ||
          NeedsAggregateFold(assignment->value)) continue;
      const auto* destination = std::get_if<abc::RegisterId>(&save->target);
      const auto* input = std::get_if<analysis::ScalarExpression>(&save->value);
      const auto* original = std::get_if<analysis::ScalarAssignment>(
          &method.instructions[i + 1].operation);
      const auto& write = method.instructions[i].effects.normal_registers.write;
      if (!destination || !input || !IsAccumulator(*input) || !original ||
          original->completion != analysis::CompletionEffect::kNormal ||
          !IsAccumulator(original->expression) ||
          !std::holds_alternative<abc::RegisterId>(original->destination) ||
          std::get<abc::RegisterId>(original->destination) != *destination ||
          !write || !std::holds_alternative<analysis::Accumulator>(*write)) continue;
      const auto value = method.ssa.Write({static_cast<std::uint32_t>(i)});
      const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(i + 1)});
      if (!value || uses[value->value] != 1 || reads.size() != 1 ||
          reads.front().value != *value) continue;
      // RHS evaluation, including throws and creation captures, stays at its
      // original PC. The destination still commits only after normal return.
      // No other observer sees the removed acc value, including handler phis.
      assignment->target = *destination;
      tail.operation = analysis::NoOperation{};
      tail.previous_origin = head.pc;
      ++i;
    }
  }
}
}  // namespace abcd::source
