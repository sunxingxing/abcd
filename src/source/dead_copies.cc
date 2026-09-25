#include "dead_copies.h"
#include "copy_origins.h"

namespace abcd::source {
namespace {
bool SlotCopy(const analysis::SemanticOperation& operation) {
  const auto* copy = std::get_if<analysis::ScalarAssignment>(&operation);
  if (!copy || copy->completion != analysis::CompletionEffect::kNormal) return false;
  const auto* value = std::get_if<analysis::ScalarValue>(&copy->expression);
  return value && (std::holds_alternative<analysis::Accumulator>(*value) ||
                   std::holds_alternative<abc::RegisterId>(*value));
}
bool PrimitiveLoad(const analysis::ScalarExpression& expression) {
  const auto* value = std::get_if<analysis::ScalarValue>(&expression);
  return value && (std::holds_alternative<analysis::SpecialValue>(*value) ||
                   std::holds_alternative<analysis::IntegerLiteral>(*value) ||
                   std::holds_alternative<analysis::Float64Bits>(*value));
}
bool DeadAssignment(const analysis::SemanticInstruction& instruction,
                    const SourceStatement& statement) {
  const auto* source = std::get_if<SourceAssignment>(&statement.operation);
  const auto* original = std::get_if<analysis::ScalarAssignment>(&instruction.operation);
  if (!source || !original || statement.fused_read || statement.fused_spill ||
      statement.fused_reload || statement.class_origin || statement.previous_origin ||
      original->completion != analysis::CompletionEffect::kNormal ||
      instruction.effects.exceptional != analysis::ExceptionalCompletion::kImpossible ||
      instruction.effects.normal != analysis::LanguageEffect::kNone ||
      instruction.effects.before_throw != analysis::LanguageEffect::kNone) return false;
  if (SlotCopy(instruction.operation)) return true;
  const auto* expression = std::get_if<analysis::ScalarExpression>(&source->value);
  return expression && PrimitiveLoad(original->expression) && PrimitiveLoad(*expression);
}
bool SameSlot(const analysis::ScalarSlot& left, const analysis::ScalarSlot& right) {
  if (left.index() != right.index()) return false;
  const auto* reg = std::get_if<abc::RegisterId>(&left);
  return !reg || *reg == std::get<abc::RegisterId>(right);
}
std::optional<analysis::ScalarSlot> CopyInput(const analysis::ScalarExpression& expression) {
  const auto* value = std::get_if<analysis::ScalarValue>(&expression);
  if (!value) return {};
  if (const auto* reg = std::get_if<abc::RegisterId>(value)) return *reg;
  if (std::holds_alternative<analysis::Accumulator>(*value)) return analysis::Accumulator{};
  return {};
}
}
std::optional<SourceFailure> ElideDeadAssignments(const AstMethod& method,
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
    if (origin && DeadAssignment(method.instructions[origin->value],
                                 ast.statements[origin->value]))
      pending.push_back(*origin);
  };
  for (std::size_t i = 0; i < uses.size(); ++i)
    enqueue(analysis::SsaValueId{static_cast<std::uint32_t>(i)});
  // Sparse DCE restricted to slot copies and primitive loads (no allocation,
  // coercion or language effects). Each definition enters once when
  // its exact count reaches zero, including chains visited out of PC order.
  // Never decrement uses for fused nodes: their replacement still consumes
  // the original values, potentially at a different generated position.
  for (std::size_t head = 0; head < pending.size(); ++head) {
    const auto instruction = pending[head];
    const auto reads = method.ssa.Reads(instruction);
    const bool copy = SlotCopy(method.instructions[instruction.value].operation);
    if ((copy && (reads.size() != 1 || reads.front().value.value >= uses.size() ||
                  !uses[reads.front().value.value])) || (!copy && !reads.empty()))
      return SourceFailure{SourceError::kInvalidEffect, ast.statements[instruction.value].pc};
    ast.statements[instruction.value].operation = analysis::NoOperation{};
    if (copy) {
      --uses[reads.front().value.value];
      enqueue(reads.front().value);
    }
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
void ElideKnownReloads(const AstMethod& method, SourceAst& ast,
                       const CopyOrigins& copies) {
  for (const auto& block : method.flow.blocks) {
    std::optional<analysis::SsaValueId> accumulator;
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t i = block.first_instruction; i < end; ++i) {
      auto& statement = ast.statements[i];
      const auto* source = std::get_if<SourceAssignment>(&statement.operation);
      const auto* original = std::get_if<analysis::ScalarAssignment>(
          &method.instructions[i].operation);
      const auto* expression = source
          ? std::get_if<analysis::ScalarExpression>(&source->value) : nullptr;
      const auto input = expression ? CopyInput(*expression) : std::nullopt;
      const auto old_input = original ? CopyInput(original->expression) : std::nullopt;
      // Fused statements may perform hidden acc writes at a different source
      // position. Even a no-op anchor ends this deliberately local proof.
      if (!source || !original || !input || !old_input ||
          original->completion != analysis::CompletionEffect::kNormal ||
          !SameSlot(source->target, original->destination) || !SameSlot(*input, *old_input) ||
          statement.fused_read || statement.fused_spill || statement.fused_reload ||
          statement.class_origin || statement.previous_origin) {
        accumulator.reset();
        continue;
      }
      const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(i)});
      if (reads.size() != 1) { accumulator.reset(); continue; }
      const auto identity = copies.At(reads.front().value);
      if (std::holds_alternative<analysis::Accumulator>(*input)) accumulator = identity;
      if (std::holds_alternative<analysis::Accumulator>(source->target)) {
        if (accumulator && *accumulator == identity)
          statement.operation = analysis::NoOperation{};
        accumulator = identity;
      }
    }
  }
}
}  // namespace abcd::source
