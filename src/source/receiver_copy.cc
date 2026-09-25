#include "receiver_copy.h"

#include <type_traits>

namespace abcd::source {
std::optional<abc::RegisterId> FindReceiverCopy(
    const analysis::SemanticMethod& method, abc::RegisterId receiver) {
  if (method.flow.blocks.empty()) return {};
  const auto& entry = method.flow.blocks.front();
  if (entry.first_instruction != 0 ||
      entry.instruction_count > method.instructions.size()) return {};
  for (std::uint32_t i = 0; i < entry.instruction_count; ++i) {
    const auto& instruction = method.instructions[i];
    const auto* assignment = std::get_if<analysis::ScalarAssignment>(&instruction.operation);
    const auto* destination = assignment
        ? std::get_if<abc::RegisterId>(&assignment->destination) : nullptr;
    const auto* value = assignment
        ? std::get_if<analysis::ScalarValue>(&assignment->expression) : nullptr;
    const auto* source = value ? std::get_if<abc::RegisterId>(value) : nullptr;
    if (!destination || !source ||
        assignment->completion != analysis::CompletionEffect::kNormal ||
        instruction.effects.normal != analysis::LanguageEffect::kNone ||
        instruction.effects.before_throw != analysis::LanguageEffect::kNone ||
        instruction.effects.exceptional != analysis::ExceptionalCompletion::kImpossible)
      return {};
    if (*source != receiver || *destination == receiver) continue;
    const auto output = method.ssa.Write({i});
    if (!output) return {};
    const auto values = method.ssa.Values();
    // Do not search another candidate after failure: that would repeat the
    // whole SSA scan. One entry receiver copy is enough for this certificate.
    for (std::size_t v = 0; v < values.size(); ++v) {
      const auto* reg = std::get_if<abc::RegisterId>(&values[v].slot);
      if (reg && *reg == *destination && v != output->value &&
          !std::holds_alternative<analysis::MethodEntry>(values[v].origin)) return {};
    }
    for (const auto& read : method.ssa.AllReads()) {
      if (read.value.value >= values.size()) return {};
      const auto* reg = std::get_if<abc::RegisterId>(&values[read.value.value].slot);
      if (reg && *reg == *destination && read.value != *output) return {};
    }
    return *destination;
  }
  return {};
}

void BindReceiverCopy(analysis::ScalarExpression& expression,
                      abc::RegisterId copy, abc::RegisterId receiver) {
  const auto bind = [&](analysis::ScalarValue& value) {
    const auto* reg = std::get_if<abc::RegisterId>(&value);
    if (reg && *reg == copy) value = receiver;
  };
  std::visit([&](auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, analysis::ScalarValue>) bind(value);
    else if constexpr (std::is_same_v<T, analysis::UnaryExpression>) bind(value.input);
    else if constexpr (std::is_same_v<T, analysis::BinaryExpression>) {
      bind(value.left);
      bind(value.right);
    } else {
      static_assert(std::is_same_v<T, analysis::PropertyRead>);
      bind(value.receiver);
      bind(value.key);
    }
  }, expression);
}
}  // namespace abcd::source
