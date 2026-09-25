#include "scalar_values.h"

#include <type_traits>

#include "copy_origins.h"

namespace abcd::source {
namespace {
enum class OperandForm { kScalar, kMember };
const analysis::ScalarValue* PureValue(const analysis::ScalarExpression& expression) {
  const auto* value = std::get_if<analysis::ScalarValue>(&expression);
  if (!value) return nullptr;
  return std::holds_alternative<analysis::Accumulator>(*value) ||
                 std::holds_alternative<abc::RegisterId>(*value) ||
                 std::holds_alternative<analysis::SpecialValue>(*value) ||
                 std::holds_alternative<analysis::IntegerLiteral>(*value) ||
                 std::holds_alternative<analysis::Float64Bits>(*value)
             ? value : nullptr;
}
bool HasMergedOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.class_origin || statement.previous_origin;
}
bool ReplaceAccumulator(analysis::ScalarExpression& expression,
                        const analysis::ScalarValue& input) {
  bool replaced = false;
  const auto replace = [&](analysis::ScalarValue& value) {
    if (std::holds_alternative<analysis::Accumulator>(value)) {
      value = input;
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
bool ElidedTransport(const AstMethod& method, const SourceAst& ast,
                     std::size_t position, std::span<const std::size_t> uses) {
  const auto& statement = ast.statements[position];
  if (!std::holds_alternative<analysis::NoOperation>(statement.operation) ||
      HasMergedOrigin(statement)) return false;
  const auto& original = method.instructions[position];
  if (original.effects.normal != analysis::LanguageEffect::kNone ||
      original.effects.before_throw != analysis::LanguageEffect::kNone ||
      original.effects.exceptional != analysis::ExceptionalCompletion::kImpossible)
    return false;
  if (std::holds_alternative<analysis::NoOperation>(original.operation)) return true;
  const auto* scalar = std::get_if<analysis::ScalarAssignment>(&original.operation);
  if (!scalar || scalar->completion != analysis::CompletionEffect::kNormal ||
      !PureValue(scalar->expression)) return false;
  const auto output = method.ssa.Write({static_cast<std::uint32_t>(position)});
  return output && uses[output->value] <= 1;
}
void FoldValues(const AstMethod& method, SourceAst& ast,
                std::span<const std::size_t> uses, OperandForm form,
                const CopyOrigins* copies = nullptr) {
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    std::optional<std::size_t> previous;
    for (std::size_t consumer_id = block.first_instruction; consumer_id < end; ++consumer_id) {
      // A bounded forward scan, never a repeated search over no-op suffixes.
      // Only the member phase crosses independently eliminated transports.
      if (form == OperandForm::kMember &&
          ElidedTransport(method, ast, consumer_id, uses)) continue;
      const auto producer_id = previous;
      previous = consumer_id;
      if (!producer_id) continue;
      const auto i = *producer_id;
      auto& head = ast.statements[i];
      auto& tail = ast.statements[consumer_id];
      const auto* load = std::get_if<SourceAssignment>(&head.operation);
      auto* assignment = std::get_if<SourceAssignment>(&tail.operation);
      if (!load || !assignment || HasMergedOrigin(head) || HasMergedOrigin(tail) ||
          !std::holds_alternative<analysis::Accumulator>(load->target)) continue;
      const auto* input = std::get_if<analysis::ScalarExpression>(&load->value);
      auto* expression = std::get_if<analysis::ScalarExpression>(&assignment->value);
      auto* member = std::get_if<MemberExpression>(&assignment->value);
      switch (form) {
        case OperandForm::kScalar: member = nullptr; break;
        case OperandForm::kMember: expression = nullptr; break;
      }
      const auto* value = input ? PureValue(*input) : nullptr;
      if (!value || (!expression && !member)) continue;
      // Preserve acc/register transport shape for the later paired reload
      // proof. Substituting a slot into a copy early can hide that pair and
      // retain more transports even though it preserves execution semantics.
      if ((std::holds_alternative<analysis::Accumulator>(*value) ||
           std::holds_alternative<abc::RegisterId>(*value)) &&
          expression && std::holds_alternative<analysis::ScalarValue>(*expression)) continue;
      const auto& consumer = method.instructions[consumer_id];
      if (member) {
        const auto* read = std::get_if<analysis::NamedRead>(&consumer.operation);
        if (!read || read->name != member->name ||
            !std::holds_alternative<analysis::Accumulator>(member->object)) continue;
      } else if (!std::holds_alternative<analysis::ScalarAssignment>(consumer.operation)) {
        continue;
      }
      const auto& original = method.instructions[i];
      const auto* scalar = std::get_if<analysis::ScalarAssignment>(&original.operation);
      if (!scalar || !PureValue(scalar->expression) ||
          !std::holds_alternative<analysis::Accumulator>(scalar->destination) ||
          scalar->completion != analysis::CompletionEffect::kNormal ||
          original.effects.normal != analysis::LanguageEffect::kNone ||
          original.effects.before_throw != analysis::LanguageEffect::kNone ||
          original.effects.exceptional != analysis::ExceptionalCompletion::kImpossible) continue;
      const auto output = method.ssa.Write({static_cast<std::uint32_t>(i)});
      if (!output || uses[output->value] != 1) continue;
      const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(consumer_id)});
      bool consumed = false;
      for (const auto& read : reads) {
        consumed |= read.value == *output;
        if (copies && uses[read.value.value] == 1)
          consumed |= copies->At(read.value) == copies->At(*output);
      }
      if (!consumed) continue;
      if (member) member->object = *value;
      else if (!ReplaceAccumulator(*expression, *value)) continue;
      // Private slot reads and primitive evaluation have no allocation or
      // coercion effects. No intervening emitted instruction redefines a slot;
      // the consumer reads its RHS before committing any destination write.
      // Handler phi uses prevent removing an observable acc commit.
      head.operation = analysis::NoOperation{};
    }
  }
}
}  // namespace

void FoldScalarValues(const AstMethod& method, SourceAst& ast,
                      std::span<const std::size_t> uses) {
  FoldValues(method, ast, uses, OperandForm::kScalar);
}
void FoldMemberReceivers(const AstMethod& method, SourceAst& ast,
                         std::span<const std::size_t> uses,
                         const CopyOrigins& copies) {
  FoldValues(method, ast, uses, OperandForm::kMember, &copies);
}
}  // namespace abcd::source
