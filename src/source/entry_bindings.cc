#include "entry_bindings.h"

#include <algorithm>
#include <type_traits>

namespace abcd::source {
void FindEntryBindings(const analysis::SemanticMethod& method,
                       ScalarNames names,
                       std::span<EntryBinding> bindings) {
  std::fill(bindings.begin(), bindings.end(), EntryBinding{});
  if (bindings.size() != names.registers.size() || method.flow.blocks.empty() ||
      names.first_parameter.value > names.registers.size() ||
      names.parameter_writes.size() >
          names.registers.size() - names.first_parameter.value) return;
  const auto immutable = [&](abc::RegisterId reg) {
    if (names.this_binding == reg) return true;
    if (reg.value < names.first_parameter.value) return false;
    const auto ordinal = reg.value - names.first_parameter.value;
    return ordinal < names.parameter_writes.size() && names.parameter_writes[ordinal] == 0;
  };
  const auto& entry = method.flow.blocks.front();
  if (entry.first_instruction != 0 ||
      entry.instruction_count > method.instructions.size()) return;
  for (std::uint32_t i = 0; i < entry.instruction_count; ++i) {
    const auto& instruction = method.instructions[i];
    const auto* assignment = std::get_if<analysis::ScalarAssignment>(&instruction.operation);
    const auto* destination = assignment
        ? std::get_if<abc::RegisterId>(&assignment->destination) : nullptr;
    const auto* value = assignment
        ? std::get_if<analysis::ScalarValue>(&assignment->expression) : nullptr;
    const auto* source = value ? std::get_if<abc::RegisterId>(value) : nullptr;
    if (!destination || !source || destination->value >= bindings.size() ||
        source->value >= bindings.size() ||
        assignment->completion != analysis::CompletionEffect::kNormal ||
        instruction.effects.normal != analysis::LanguageEffect::kNone ||
        instruction.effects.before_throw != analysis::LanguageEffect::kNone ||
        instruction.effects.exceptional != analysis::ExceptionalCompletion::kImpossible)
      break;
    if (!immutable(*source) || *destination == *source) continue;
    if (const auto output = method.ssa.Write({i}))
      bindings[destination->value] = {*source, *output};
  }
  const auto values = method.ssa.Values();
  for (std::size_t v = 0; v < values.size(); ++v) {
    const auto* reg = std::get_if<abc::RegisterId>(&values[v].slot);
    if (!reg || reg->value >= bindings.size()) continue;
    auto& binding = bindings[reg->value];
    if (binding.source && v != binding.definition.value &&
        !std::holds_alternative<analysis::MethodEntry>(values[v].origin))
      binding.source.reset();
  }
  for (const auto& read : method.ssa.AllReads()) {
    if (read.value.value >= values.size()) {
      std::fill(bindings.begin(), bindings.end(), EntryBinding{});
      return;
    }
    const auto* reg = std::get_if<abc::RegisterId>(&values[read.value.value].slot);
    if (!reg || reg->value >= bindings.size()) continue;
    auto& binding = bindings[reg->value];
    if (binding.source && read.value != binding.definition) binding.source.reset();
  }
}

void BindEntryReads(analysis::ScalarExpression& expression,
                    std::span<const EntryBinding> bindings) {
  const auto bind = [&](analysis::ScalarValue& value) {
    const auto* reg = std::get_if<abc::RegisterId>(&value);
    if (reg && reg->value < bindings.size() && bindings[reg->value].source)
      value = *bindings[reg->value].source;
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
