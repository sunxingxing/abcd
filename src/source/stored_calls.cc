#include "stored_calls.h"

#include "private_copy.h"

namespace abcd::source {
namespace {
bool HasMergedOrigin(const SourceStatement& node) {
  return node.fused_read || node.fused_spill || node.fused_reload ||
         node.previous_origin || node.class_origin || node.literal_origin;
}
}  // namespace

void FoldStoredCalls(const AstMethod& method, SourceAst& ast,
                     std::span<const std::size_t> uses) {
  if (method.ssa.IsPhased() || ast.regions.catches) return;
  // Visit each owner once, inspecting its indexed call and adjacent save.
  // The Reference and argument interval stay in place; only the normal result
  // commit changes. Never infer result liveness from the owner's getter SSA.
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t i = block.first_instruction; i < end; ++i) {
      auto& head = ast.statements[i];
      auto* call = std::get_if<SourceStagedCall>(&head.operation);
      if (!call || call->stored_result || call->return_statement ||
          call->result != AssignmentResult::kStored || HasMergedOrigin(head)) continue;
      const auto invoked = std::size_t(call->call_statement.value);
      if (invoked <= i || invoked >= end || invoked + 1 >= end) continue;
      const auto& anchor = ast.statements[invoked];
      auto& tail = ast.statements[invoked + 1];
      if (!std::holds_alternative<analysis::NoOperation>(anchor.operation) ||
          HasMergedOrigin(anchor) || HasMergedOrigin(tail) ||
          !std::holds_alternative<analysis::ReceiverCall>(
              method.instructions[invoked].operation)) continue;
      const auto* save = std::get_if<SourceAssignment>(&tail.operation);
      const auto* target = save ? std::get_if<abc::RegisterId>(&save->target) : nullptr;
      const auto* scalar = save
          ? std::get_if<analysis::ScalarExpression>(&save->value) : nullptr;
      const auto* input = scalar ? std::get_if<analysis::ScalarValue>(scalar) : nullptr;
      const auto& instruction = method.instructions[invoked + 1];
      const auto* copy = std::get_if<analysis::ScalarAssignment>(&instruction.operation);
      const auto* original = copy
          ? std::get_if<analysis::ScalarValue>(&copy->expression) : nullptr;
      const auto* destination = copy
          ? std::get_if<abc::RegisterId>(&copy->destination) : nullptr;
      if (!target || save->result != AssignmentResult::kStored || save->object_field ||
          !input || !std::holds_alternative<analysis::Accumulator>(*input) ||
          !destination || *destination != *target || !original ||
          !std::holds_alternative<analysis::Accumulator>(*original) ||
          copy->completion != analysis::CompletionEffect::kNormal ||
          !IsPureCopyStep(instruction)) continue;
      const auto output = method.ssa.Write(call->call_statement);
      const auto reads = method.ssa.Reads(
          {static_cast<std::uint32_t>(invoked + 1)});
      const auto slot = output ? method.ssa.Slot(*output) : std::nullopt;
      if (!output || output->value >= uses.size() || uses[output->value] != 1 ||
          !slot || !std::holds_alternative<analysis::Accumulator>(*slot) ||
          reads.size() != 1 || reads.front().value != *output) continue;
      call->stored_result = CallResultStore{
          *target, analysis::InstructionId{static_cast<std::uint32_t>(invoked + 1)}};
      tail.operation = analysis::NoOperation{};
      tail.previous_origin = anchor.pc;
    }
  }
}
}  // namespace abcd::source
