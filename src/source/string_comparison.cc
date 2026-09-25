#include "string_comparison.h"

namespace abcd::source {
namespace {
bool HasMergedOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.class_origin || statement.previous_origin;
}
}  // namespace

void FoldStringComparisons(const AstMethod& method, SourceAst& ast,
                           std::span<const std::size_t> uses) {
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t i = block.first_instruction; i + 1 < end; ++i) {
      auto& head = ast.statements[i];
      auto& tail = ast.statements[i + 1];
      const auto* load = std::get_if<SourceAssignment>(&head.operation);
      auto* assignment = std::get_if<SourceAssignment>(&tail.operation);
      const auto* string = load ? std::get_if<StringExpression>(&load->value) : nullptr;
      auto* scalar = assignment
          ? std::get_if<analysis::ScalarExpression>(&assignment->value) : nullptr;
      const auto* binary = scalar ? std::get_if<analysis::BinaryExpression>(scalar) : nullptr;
      if (!string || !binary || HasMergedOrigin(head) || HasMergedOrigin(tail) ||
          !std::holds_alternative<analysis::Accumulator>(load->target) ||
          !std::holds_alternative<analysis::Accumulator>(binary->right) ||
          std::holds_alternative<analysis::Accumulator>(binary->left) ||
          std::holds_alternative<analysis::EmptyArray>(binary->left)) continue;
      const auto equal = binary->op == analysis::BinaryOperator::kStrictEqual;
      if (!equal && binary->op != analysis::BinaryOperator::kStrictNotEqual) continue;
      const auto& producer = method.instructions[i];
      const auto* original_load = std::get_if<analysis::StringLoad>(&producer.operation);
      const auto* original = std::get_if<analysis::ScalarAssignment>(
          &method.instructions[i + 1].operation);
      const auto* comparison = original
          ? std::get_if<analysis::BinaryExpression>(&original->expression) : nullptr;
      const auto& write = producer.effects.normal_registers.write;
      if (!original_load || original_load->value != string->value ||
          !write || !std::holds_alternative<analysis::Accumulator>(*write) ||
          producer.effects.normal != analysis::LanguageEffect::kNone ||
          producer.effects.before_throw != analysis::LanguageEffect::kNone ||
          producer.effects.exceptional != analysis::ExceptionalCompletion::kImpossible ||
          !comparison || comparison->op != binary->op ||
          !std::holds_alternative<analysis::Accumulator>(comparison->right) ||
          original->completion != analysis::CompletionEffect::kNormal) continue;
      const auto output = method.ssa.Write({static_cast<std::uint32_t>(i)});
      if (!output || uses[output->value] != 1) continue;
      bool consumed = false;
      for (const auto& read : method.ssa.Reads({static_cast<std::uint32_t>(i + 1)}))
        consumed |= read.value == *output;
      if (!consumed) continue;
      // Strict equality performs no coercion. Only a private slot/primitive
      // read precedes the deferred string value; keep the comparison's PC.
      const StringComparison folded{binary->left, string->value,
          equal ? StrictEquality::kEqual : StrictEquality::kNotEqual};
      assignment->value = folded;
      head.operation = analysis::NoOperation{};
      ++i;
    }
  }
}
}  // namespace abcd::source
