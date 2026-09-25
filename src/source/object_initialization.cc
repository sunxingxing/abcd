#include "object_initialization.h"

#include "private_copy.h"
#include "abcd/source/source_contract.h"

namespace abcd::source {
namespace {
bool Merged(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.previous_origin || statement.class_origin || statement.literal_origin;
}
}
void FoldObjectInitialization(const AstMethod& method, SourceAst& ast,
                              std::span<std::size_t> uses) {
  if (method.ssa.IsPhased() || ast.regions.catches ||
      ResourceFailures(kSourceContract) != ResourceFailureScope::kExcluded) return;
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t i = block.first_instruction; i + 1 < end; ++i) {
      auto& head = ast.statements[i];
      auto* assignment = std::get_if<SourceAssignment>(&head.operation);
      const auto* buffer = assignment
          ? std::get_if<analysis::BufferedObjectCreation>(&assignment->value) : nullptr;
      const auto* original = std::get_if<analysis::BufferedObjectCreation>(
          &method.instructions[i].operation);
      if (!buffer || !original || buffer->literal != original->literal ||
          Merged(head) || assignment->object_field ||
          assignment->result != AssignmentResult::kStored ||
          !std::holds_alternative<analysis::Accumulator>(assignment->target)) continue;
      auto& save_node = ast.statements[i + 1];
      const auto* save = std::get_if<SourceAssignment>(&save_node.operation);
      const auto* target = save ? std::get_if<abc::RegisterId>(&save->target) : nullptr;
      const auto* scalar = save ? std::get_if<analysis::ScalarExpression>(&save->value) : nullptr;
      const auto* input = scalar ? std::get_if<analysis::ScalarValue>(scalar) : nullptr;
      const auto* original_save = std::get_if<analysis::ScalarAssignment>(
          &method.instructions[i + 1].operation);
      const auto* original_input = original_save
          ? std::get_if<analysis::ScalarValue>(&original_save->expression) : nullptr;
      const auto* original_target = original_save
          ? std::get_if<abc::RegisterId>(&original_save->destination) : nullptr;
      if (!target || !input || !std::holds_alternative<analysis::Accumulator>(*input) ||
          !original_target || *original_target != *target || !original_input ||
          !std::holds_alternative<analysis::Accumulator>(*original_input) ||
          original_save->completion != analysis::CompletionEffect::kNormal ||
          Merged(save_node) || save->object_field ||
          save->result != AssignmentResult::kStored ||
          !IsPureCopyStep(method.instructions[i + 1])) continue;
      const auto output = method.ssa.Write({static_cast<std::uint32_t>(i)});
      const auto saved = method.ssa.Write({static_cast<std::uint32_t>(i + 1)});
      const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(i + 1)});
      if (!output || !saved || output->value >= uses.size() ||
          saved->value >= uses.size() || uses[saved->value] == 0 ||
          uses[output->value] != 1 || reads.size() != 1 || reads.front().value != *output)
        continue;
      auto field = i + 2;
      std::optional<analysis::InstructionId> allocation;
      for (; field < end; ++field) {
        const auto& node = ast.statements[field];
        if (!std::holds_alternative<analysis::NoOperation>(node.operation) || Merged(node)) break;
        const auto& instruction = method.instructions[field];
        const auto* copy = std::get_if<analysis::ScalarAssignment>(&instruction.operation);
        const auto* value = copy ? std::get_if<analysis::ScalarValue>(&copy->expression) : nullptr;
        if (!value || copy->completion != analysis::CompletionEffect::kNormal) break;
        if (std::holds_alternative<analysis::EmptyArray>(*value)) {
          if (allocation || !std::holds_alternative<analysis::Accumulator>(copy->destination) ||
              instruction.effects.normal != analysis::LanguageEffect::kAllocation ||
              instruction.effects.before_throw != analysis::LanguageEffect::kNone ||
              instruction.effects.exceptional != analysis::ExceptionalCompletion::kImpossible)
            break;
          allocation = analysis::InstructionId{static_cast<std::uint32_t>(field)};
        } else if ((!std::holds_alternative<analysis::Accumulator>(*value) &&
                    !std::holds_alternative<abc::RegisterId>(*value)) ||
                   !IsPureCopyStep(instruction)) break;
      }
      if (field >= end || !allocation) continue;
      auto& tail = ast.statements[field];
      const auto* store = std::get_if<SourceOwnStore>(&tail.operation);
      const auto* original_store = std::get_if<analysis::OwnWrite>(
          &method.instructions[field].operation);
      const auto* value = store ? std::get_if<analysis::ScalarValue>(&store->value) : nullptr;
      if (!value || !std::holds_alternative<analysis::EmptyArray>(*value) || Merged(tail) ||
          !original_store || original_store->object != store->write.object ||
          original_store->name != store->write.name || original_store->kind != store->write.kind ||
          store->write.object != *target || store->origin.literal != buffer->literal) continue;
      const auto array = method.ssa.Write(*allocation);
      if (!array || array->value >= uses.size() || uses[array->value] != 1) continue;
      const auto inputs = method.ssa.Reads({static_cast<std::uint32_t>(field)});
      std::size_t receiver_reads = 0;
      bool rhs = false;
      for (const auto& read : inputs) {
        receiver_reads += read.value == *saved;
        rhs |= read.value == *array;
      }
      if (receiver_reads != 1 || !rhs || *saved == *array) continue;
      // No emitted operation observes the object between allocation and field
      // initialization. Buffer values are primitive and the writer verifies
      // that this key already exists, preserving order and data descriptors.
      assignment->object_field = ObjectField{store->write.name, *value};
      // Retain the original save's SSA identity through the copy anchor. Only
      // the certified own-store receiver read disappears; phi/later reads stay
      // live. Argument folding may then remove a sole remaining call commit.
      assignment->target = *target;
      save_node.operation = analysis::NoOperation{};
      save_node.previous_origin = head.pc;
      --uses[saved->value];
      tail.operation = analysis::NoOperation{};
      tail.literal_origin = analysis::InstructionId{static_cast<std::uint32_t>(i)};
      ast.statements[allocation->value].literal_origin = tail.literal_origin;
      i = field;
    }
  }
}
}  // namespace abcd::source
