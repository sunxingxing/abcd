#include "scalar_constants.h"

#include <type_traits>

namespace abcd::source {
namespace {
const analysis::ScalarValue* Primitive(const analysis::ScalarExpression& expression) {
  const auto* value = std::get_if<analysis::ScalarValue>(&expression);
  if (!value) return nullptr;
  return std::holds_alternative<analysis::SpecialValue>(*value) ||
                 std::holds_alternative<analysis::IntegerLiteral>(*value) ||
                 std::holds_alternative<analysis::Float64Bits>(*value)
             ? value : nullptr;
}
bool HasMergedOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.class_origin || statement.previous_origin;
}
bool ReplaceAccumulator(analysis::ScalarExpression& expression,
                        const analysis::ScalarValue& constant) {
  bool replaced = false;
  const auto replace = [&](analysis::ScalarValue& value) {
    if (std::holds_alternative<analysis::Accumulator>(value)) {
      value = constant;
      replaced = true;
    }
  };
  std::visit([&](auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, analysis::ScalarValue>) replace(value);
    else if constexpr (std::is_same_v<T, analysis::UnaryExpression>) replace(value.input);
    else if constexpr (std::is_same_v<T, analysis::BinaryExpression>) {
      replace(value.left);
      replace(value.right);
    } else {
      static_assert(std::is_same_v<T, analysis::PropertyRead>);
      replace(value.receiver);
      replace(value.key);
    }
  }, expression);
  return replaced;
}
}  // namespace

void FoldScalarConstants(const AstMethod& method, SourceAst& ast,
                         std::span<const std::size_t> uses) {
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t i = block.first_instruction; i + 1 < end; ++i) {
      auto& head = ast.statements[i];
      auto& tail = ast.statements[i + 1];
      const auto* load = std::get_if<SourceAssignment>(&head.operation);
      auto* assignment = std::get_if<SourceAssignment>(&tail.operation);
      if (!load || !assignment || HasMergedOrigin(head) || HasMergedOrigin(tail) ||
          !std::holds_alternative<analysis::Accumulator>(load->target)) continue;
      const auto* input = std::get_if<analysis::ScalarExpression>(&load->value);
      auto* expression = std::get_if<analysis::ScalarExpression>(&assignment->value);
      const auto* constant = input ? Primitive(*input) : nullptr;
      if (!constant || !expression) continue;
      const auto& original = method.instructions[i];
      const auto* scalar = std::get_if<analysis::ScalarAssignment>(&original.operation);
      if (!scalar || !Primitive(scalar->expression) ||
          !std::holds_alternative<analysis::Accumulator>(scalar->destination) ||
          scalar->completion != analysis::CompletionEffect::kNormal ||
          original.effects.normal != analysis::LanguageEffect::kNone ||
          original.effects.before_throw != analysis::LanguageEffect::kNone ||
          original.effects.exceptional != analysis::ExceptionalCompletion::kImpossible ||
          !std::holds_alternative<analysis::ScalarAssignment>(
              method.instructions[i + 1].operation)) continue;
      const auto output = method.ssa.Write({static_cast<std::uint32_t>(i)});
      if (!output || uses[output->value] != 1) continue;
      const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(i + 1)});
      bool consumed = false;
      for (const auto& read : reads) consumed |= read.value == *output;
      if (!consumed || !ReplaceAccumulator(*expression, *constant)) continue;
      // Primitive evaluation has no allocation/coercion effects. The consumer
      // still executes at its own PC, and handler phi uses prevent removing an
      // observable acc commit. No printed-text or numeric-value inference.
      head.operation = analysis::NoOperation{};
    }
  }
}
}  // namespace abcd::source
