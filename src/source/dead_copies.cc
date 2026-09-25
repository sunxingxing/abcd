#include "dead_copies.h"

namespace abcd::source {
namespace {
bool SlotCopy(const analysis::SemanticOperation& operation) {
  const auto* copy = std::get_if<analysis::ScalarAssignment>(&operation);
  if (!copy || copy->completion != analysis::CompletionEffect::kNormal) return false;
  const auto* value = std::get_if<analysis::ScalarValue>(&copy->expression);
  return value && (std::holds_alternative<analysis::Accumulator>(*value) ||
                   std::holds_alternative<abc::RegisterId>(*value));
}
}
std::optional<SourceFailure> ElideDeadCopies(const AstMethod& method,
    SourceAst& ast, std::span<std::size_t> uses, std::size_t bytes) {
  if (method.instructions.size() > bytes / sizeof(analysis::InstructionId))
    return SourceFailure{SourceError::kResourceLimit, {}};
  std::vector<analysis::InstructionId> pending;
  pending.reserve(method.instructions.size());
  if (pending.capacity() > bytes / sizeof(analysis::InstructionId))
    return SourceFailure{SourceError::kResourceLimit, {}};
  const auto enqueue = [&](analysis::SsaValueId value) {
    if (uses[value.value]) return;
    const auto origin = method.ssa.Definition(value);
    if (origin && SlotCopy(method.instructions[origin->value].operation) &&
        std::holds_alternative<SourceAssignment>(ast.statements[origin->value].operation))
      pending.push_back(*origin);
  };
  for (std::size_t i = 0; i < uses.size(); ++i)
    enqueue(analysis::SsaValueId{static_cast<std::uint32_t>(i)});
  // Sparse DCE restricted to slot copies. Each definition enters once when
  // its exact count reaches zero, including chains visited out of PC order.
  // Never decrement uses for fused nodes: their replacement still consumes
  // the original values, potentially at a different generated position.
  for (std::size_t head = 0; head < pending.size(); ++head) {
    const auto instruction = pending[head];
    const auto reads = method.ssa.Reads(instruction);
    if (reads.size() != 1 || reads.front().value.value >= uses.size() ||
        !uses[reads.front().value.value])
      return SourceFailure{SourceError::kInvalidEffect, ast.statements[instruction.value].pc};
    ast.statements[instruction.value].operation = analysis::NoOperation{};
    --uses[reads.front().value.value];
    enqueue(reads.front().value);
  }
  return {};
}
void ElideCopyReloads(const AstMethod& method, SourceAst& ast,
                     std::span<const std::size_t> uses) {
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t i = block.first_instruction; i + 1 < end; ++i) {
      auto& spill = ast.statements[i];
      auto& reload = ast.statements[i + 1];
      const auto* save = std::get_if<SourceAssignment>(&spill.operation);
      const auto* load = std::get_if<SourceAssignment>(&reload.operation);
      if (!save || !load || !SlotCopy(method.instructions[i].operation) ||
          !SlotCopy(method.instructions[i + 1].operation) ||
          spill.fused_read || spill.fused_spill || spill.fused_reload ||
          spill.class_origin || spill.previous_origin ||
          reload.fused_read || reload.fused_spill || reload.fused_reload ||
          reload.class_origin || reload.previous_origin) continue;
      const auto* reg = std::get_if<abc::RegisterId>(&save->target);
      if (!reg || !std::holds_alternative<analysis::Accumulator>(load->target)) continue;
      const auto* saved_expression = std::get_if<analysis::ScalarExpression>(&save->value);
      const auto* loaded_expression = std::get_if<analysis::ScalarExpression>(&load->value);
      const auto* saved_value = saved_expression
          ? std::get_if<analysis::ScalarValue>(saved_expression) : nullptr;
      const auto* loaded_value = loaded_expression
          ? std::get_if<analysis::ScalarValue>(loaded_expression) : nullptr;
      const auto* loaded_register = loaded_value
          ? std::get_if<abc::RegisterId>(loaded_value) : nullptr;
      if (!saved_value || !std::holds_alternative<analysis::Accumulator>(*saved_value) ||
          !loaded_register || *loaded_register != *reg) continue;
      const auto saved = method.ssa.Write({static_cast<std::uint32_t>(i)});
      const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(i + 1)});
      if (!saved || uses[saved->value] != 1 || reads.size() != 1 ||
          reads.front().value != *saved) continue;
      // No branch/handler can enter at the reload inside this normal block.
      // Both operations are nonthrowing slot copies; acc already holds the
      // reloaded value. Never generalize this to copies across an effect.
      spill.operation = analysis::NoOperation{};
      reload.operation = analysis::NoOperation{};
      ++i;
    }
  }
}
}  // namespace abcd::source
