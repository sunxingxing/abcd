#include "stored_expression.h"

#include "expression_origins.h"

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
bool FoldMemberValue(const AstMethod& method, std::size_t position,
                     SourceStatement& head, SourceStatement& tail,
                     std::span<const std::size_t> uses) {
  const auto* assignment = std::get_if<SourceAssignment>(&head.operation);
  auto* member = std::get_if<SourceMemberStore>(&tail.operation);
  if (!assignment || !member || HasMergedOrigin(head) || HasMergedOrigin(tail) ||
      !std::holds_alternative<analysis::Accumulator>(assignment->target) ||
      !std::holds_alternative<analysis::Accumulator>(member->value)) return false;
  const auto* expression = std::get_if<analysis::ScalarExpression>(&assignment->value);
  const auto* value = expression ? std::get_if<analysis::ScalarValue>(expression) : nullptr;
  if (!value || std::holds_alternative<analysis::EmptyArray>(*value)) return false;
  const auto& producer = method.instructions[position];
  const auto* scalar = std::get_if<analysis::ScalarAssignment>(&producer.operation);
  const auto* original_value = scalar
      ? std::get_if<analysis::ScalarValue>(&scalar->expression) : nullptr;
  const auto* store = std::get_if<analysis::NamedWrite>(
      &method.instructions[position + 1].operation);
  const auto* receiver = std::get_if<abc::RegisterId>(&member->object);
  if (!original_value || std::holds_alternative<analysis::EmptyArray>(*original_value) ||
      !std::holds_alternative<analysis::Accumulator>(scalar->destination) ||
      scalar->completion != analysis::CompletionEffect::kNormal ||
      producer.effects.normal != analysis::LanguageEffect::kNone ||
      producer.effects.before_throw != analysis::LanguageEffect::kNone ||
      producer.effects.exceptional != analysis::ExceptionalCompletion::kImpossible ||
      !store || !receiver || *receiver != store->object || member->name != store->name)
    return false;
  const auto output = method.ssa.Write({static_cast<std::uint32_t>(position)});
  if (!output || uses[output->value] != 1) return false;
  const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(position + 1)});
  bool consumed = false;
  for (const auto& read : reads) consumed |= read.value == *output;
  if (!consumed) return false;
  // Only private slot reads or primitive constants move past receiver/key
  // evaluation. The named Set stays at its original PC with all setter/Proxy
  // effects. Exact uses include handler phis observing the removed acc write.
  member->value = *value;
  head.operation = analysis::NoOperation{};
  return true;
}
}  // namespace

void FoldStoredExpressions(const AstMethod& method, SourceAst& ast,
                           std::span<const std::size_t> uses) {
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t i = block.first_instruction; i + 1 < end; ++i) {
      auto& head = ast.statements[i];
      auto& tail = ast.statements[i + 1];
      if (FoldMemberValue(method, i, head, tail, uses)) {
        ++i;
        continue;
      }
      auto* assignment = std::get_if<SourceAssignment>(&head.operation);
      const auto* save = std::get_if<SourceAssignment>(&tail.operation);
      const auto* local = std::get_if<SourceLocalStore>(&tail.operation);
      if (!assignment || (!save && !local) || HasMergedOrigin(tail) ||
          !CanRetargetExpression(head, assignment->value) ||
          !std::holds_alternative<analysis::Accumulator>(assignment->target) ||
          NeedsAggregateFold(assignment->value)) continue;
      std::optional<abc::RegisterId> destination;
      const auto& original = method.instructions[i + 1];
      if (save) {
        const auto* reg = std::get_if<abc::RegisterId>(&save->target);
        const auto* input = std::get_if<analysis::ScalarExpression>(&save->value);
        const auto* copy = std::get_if<analysis::ScalarAssignment>(&original.operation);
        if (!reg || !input || !IsAccumulator(*input) || !copy ||
            copy->completion != analysis::CompletionEffect::kNormal ||
            !IsAccumulator(copy->expression) ||
            !std::holds_alternative<abc::RegisterId>(copy->destination) ||
            std::get<abc::RegisterId>(copy->destination) != *reg) continue;
        destination = *reg;
      } else {
        const auto* store = std::get_if<analysis::LexicalStore>(&original.operation);
        if (!store || !std::holds_alternative<analysis::Accumulator>(store->value) ||
            !std::holds_alternative<analysis::Accumulator>(local->value) ||
            original.effects.exceptional != analysis::ExceptionalCompletion::kImpossible)
          continue;
      }
      const auto& write = method.instructions[i].effects.normal_registers.write;
      if (!write || !std::holds_alternative<analysis::Accumulator>(*write)) continue;
      const auto value = method.ssa.Write({static_cast<std::uint32_t>(i)});
      const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(i + 1)});
      if (!value || uses[value->value] != 1 || reads.size() != 1 ||
          reads.front().value != *value) continue;
      // RHS evaluation, including throws and creation captures, stays at its
      // original PC. The destination still commits only after normal return.
      // No other observer sees the removed acc value, including handler phis.
      if (destination) {
        assignment->target = *destination;
      } else {
        // LocalCellId comes from the existing environment/initialization
        // certificate. Never infer a cell or declaration from a register name.
        auto expression = std::move(assignment->value);
        head.operation = SourceLocalAssignment{local->cell, std::move(expression)};
      }
      tail.operation = analysis::NoOperation{};
      tail.previous_origin = head.pc;
      ++i;
    }
  }
}
}  // namespace abcd::source
