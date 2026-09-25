#include "entry_bindings.h"

#include <algorithm>
#include <type_traits>

#include "abcd/source/semantic_source.h"
#include "register_commits.h"

namespace abcd::source {
namespace {
void CheckEmittedWrites(const SourceAst& ast, std::span<EntryBinding> bindings) {
  VisitRegisterCommits(ast, [&](const SourceStatement& node, abc::RegisterId target) {
    if (target.value >= bindings.size() || !bindings[target.value].source) return;
    const auto* assignment = std::get_if<SourceAssignment>(&node.operation);
    const auto* expression = assignment
        ? std::get_if<analysis::ScalarExpression>(&assignment->value) : nullptr;
    const auto* value = expression ? std::get_if<analysis::ScalarValue>(expression) : nullptr;
    const auto* source = value ? std::get_if<abc::RegisterId>(value) : nullptr;
    if (!assignment || assignment->object_field || !source || bindings[target.value].source != *source)
      bindings[target.value].source.reset();
  });
}
}  // namespace
void FindEntryBindings(const analysis::SemanticMethod& method,
                       const SourceAst& ast, ScalarNames names,
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
      continue;
    if (!immutable(*source) || *destination == *source) continue;
    if (const auto output = method.ssa.Write({i}))
      bindings[destination->value] = {*source, *output};
  }
  const auto values = method.ssa.Values();
  // Native literal defaults have already removed their guarded local write.
  // Both original SSA alternatives now denote the same initialized formal.
  // This certificate belongs to the common callable/body pair, not to a
  // constructor-specific naming convention.
  for (const auto& value : ast.parameter_defaults) {
    if (value.first.value >= method.instructions.size() ||
        value.end.value <= value.first.value || value.end.value > method.instructions.size()) continue;
    const auto reads = method.ssa.Reads(value.first);
    const auto initialized = method.ssa.Write({value.end.value - 1});
    if (reads.size() != 1 || !initialized) continue;
    const auto* reg = std::get_if<abc::RegisterId>(&values[reads.front().value.value].slot);
    if (!reg || reg->value >= bindings.size()) continue;
    auto& binding = bindings[reg->value];
    const auto* written = std::get_if<abc::RegisterId>(&values[initialized->value].slot);
    if (!binding.source || binding.source->value < names.first_parameter.value ||
        binding.source->value - names.first_parameter.value != value.parameter ||
        binding.definition != reads.front().value || !written || *written != *reg) continue;
    binding.initialized = initialized;
  }
  for (std::size_t v = 0; v < values.size(); ++v) {
    const auto* reg = std::get_if<abc::RegisterId>(&values[v].slot);
    if (!reg || reg->value >= bindings.size()) continue;
    auto& binding = bindings[reg->value];
    if (binding.source && v != binding.definition.value &&
        (!binding.initialized || (v != binding.initialized->value &&
         !std::holds_alternative<analysis::PhiValue>(values[v].origin))) &&
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
    if (binding.source && read.value != binding.definition &&
        (!binding.initialized || (read.value != *binding.initialized &&
         !std::holds_alternative<analysis::PhiValue>(values[read.value.value].origin))))
      binding.source.reset();
  }
  // A phi web may merge only the copy and removed default initialization.
  // All other instruction definitions were rejected above; reject entry
  // seeds too, including loop paths that could read before the copy.
  for (std::size_t v = 0; v < values.size(); ++v) {
    if (!std::holds_alternative<analysis::PhiValue>(values[v].origin)) continue;
    const auto* reg = std::get_if<abc::RegisterId>(&values[v].slot);
    if (!reg || reg->value >= bindings.size()) continue;
    auto& binding = bindings[reg->value];
    if (!binding.source || !binding.initialized) continue;
    for (const auto& input : method.ssa.Inputs({static_cast<std::uint32_t>(v)})) {
      const auto& value = values[input.value.value];
      const auto* slot = std::get_if<abc::RegisterId>(&value.slot);
      if (!slot || *slot != *reg || std::holds_alternative<analysis::MethodEntry>(value.origin)) {
        binding.source.reset();
        break;
      }
    }
  }
  // Original SSA alone cannot certify physical writes after retargeting.
  // Keep only emitted commits that still copy the immutable formal verbatim.
  CheckEmittedWrites(ast, bindings);
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
