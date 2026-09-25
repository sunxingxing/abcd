#include "store_receivers.h"

namespace abcd::source {
namespace {
bool HasMergedOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.class_origin || statement.previous_origin;
}
bool IsPure(const analysis::SemanticInstruction& instruction) {
  return instruction.effects.normal == analysis::LanguageEffect::kNone &&
         instruction.effects.before_throw == analysis::LanguageEffect::kNone &&
         instruction.effects.exceptional ==
             analysis::ExceptionalCompletion::kImpossible;
}
}  // namespace

void FoldStoreReceivers(const AstMethod& method, SourceAst& ast,
                        std::span<const std::size_t> uses) {
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    std::optional<std::size_t> previous;
    for (std::size_t i = block.first_instruction; i < end; ++i) {
      auto& tail = ast.statements[i];
      const auto& original = method.instructions[i];
      // These operations were independently eliminated. Crossing their empty
      // anchors cannot change a private slot; effects and fused origins fence
      // the scan even if an earlier transformation emitted no text there.
      if (std::holds_alternative<analysis::NoOperation>(tail.operation) &&
          !HasMergedOrigin(tail) && IsPure(original)) continue;
      const auto producer = previous;
      previous = i;
      if (!producer || HasMergedOrigin(tail)) continue;
      auto* store = std::get_if<SourceMemberStore>(&tail.operation);
      const auto* write = std::get_if<analysis::NamedWrite>(&original.operation);
      const auto* receiver = store ? std::get_if<abc::RegisterId>(&store->object) : nullptr;
      if (!receiver || !write || *receiver != write->object ||
          store->name != write->name) continue;
      auto& head = ast.statements[*producer];
      const auto* assignment = std::get_if<SourceAssignment>(&head.operation);
      const auto* target = assignment
          ? std::get_if<abc::RegisterId>(&assignment->target) : nullptr;
      const auto* expression = assignment
          ? std::get_if<analysis::ScalarExpression>(&assignment->value) : nullptr;
      const auto* value = expression
          ? std::get_if<analysis::ScalarValue>(expression) : nullptr;
      if (!target || *target != *receiver || !value || HasMergedOrigin(head) ||
          std::holds_alternative<analysis::EmptyArray>(*value)) continue;
      const auto& source = method.instructions[*producer];
      const auto* copy = std::get_if<analysis::ScalarAssignment>(&source.operation);
      const auto* destination = copy
          ? std::get_if<abc::RegisterId>(&copy->destination) : nullptr;
      if (!destination || *destination != *target || !IsPure(source) ||
          copy->completion != analysis::CompletionEffect::kNormal ||
          !std::holds_alternative<analysis::ScalarValue>(copy->expression)) continue;
      const auto output = method.ssa.Write({static_cast<std::uint32_t>(*producer)});
      if (!output || uses[output->value] != 1) continue;
      bool consumed = false;
      for (const auto& read : method.ssa.Reads({static_cast<std::uint32_t>(i)}))
        consumed |= read.value == *output;
      if (!consumed) continue;
      // No emitted evaluation lies between the private value read and Set.
      // Single use also excludes observing the removed register commit from
      // an exception handler. Keep Set and its source span at the original PC.
      store->object = *value;
      head.operation = analysis::NoOperation{};
    }
  }
}
}  // namespace abcd::source
