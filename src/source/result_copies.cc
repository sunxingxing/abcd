#include "result_copies.h"

#include "expression_origins.h"
#include "private_copy.h"

namespace abcd::source {
namespace {
bool EmptyCopyStep(const AstMethod& method, const SourceAst& ast, std::size_t i) {
  const auto& node = ast.statements[i];
  if (!std::holds_alternative<analysis::NoOperation>(node.operation) ||
      node.fused_read || node.fused_spill || node.fused_reload ||
      node.class_origin || node.literal_origin ||
      (node.previous_origin && (!i || *node.previous_origin != ast.statements[i - 1].pc)))
    return false;
  // A typed SuperCheck is empty only after BuildSourceAst's initialization
  // certificate. Never cross a still-emitted check or an arbitrary effect.
  return IsPureCopyStep(method.instructions[i]) ||
         std::holds_alternative<analysis::SuperCheck>(method.instructions[i].operation);
}
}  // namespace

void FoldResultCopies(const AstMethod& method, SourceAst& ast,
                      std::span<const std::size_t> uses) {
  if (method.ssa.IsPhased() || ast.regions.catches) return;
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    std::optional<std::size_t> previous;
    for (std::size_t i = block.first_instruction; i < end; ++i) {
      if (EmptyCopyStep(method, ast, i)) continue;
      const auto producer = previous;
      previous = i;
      if (!producer) continue;
      auto& head = ast.statements[*producer];
      auto& tail = ast.statements[i];
      auto* result = std::get_if<SourceAssignment>(&head.operation);
      const auto* copy = std::get_if<SourceAssignment>(&tail.operation);
      const auto* from = result ? std::get_if<abc::RegisterId>(&result->target) : nullptr;
      const auto* to = copy ? std::get_if<abc::RegisterId>(&copy->target) : nullptr;
      const auto* scalar = copy ? std::get_if<analysis::ScalarExpression>(&copy->value) : nullptr;
      const auto* value = scalar ? std::get_if<analysis::ScalarValue>(scalar) : nullptr;
      const auto* source = value ? std::get_if<abc::RegisterId>(value) : nullptr;
      if (!from || !to || !source || *source != *from || *from == *to ||
          result->result != AssignmentResult::kStored || copy->result != AssignmentResult::kStored ||
          result->object_field || copy->object_field || head.literal_origin ||
          tail.literal_origin || !CanRetargetExpression(head, result->value) ||
          tail.fused_read || tail.fused_spill || tail.fused_reload ||
          tail.previous_origin || tail.class_origin) continue;
      const auto* original = std::get_if<analysis::ScalarAssignment>(&method.instructions[i].operation);
      const auto* input = original ? std::get_if<analysis::ScalarValue>(&original->expression) : nullptr;
      const auto* reg = input ? std::get_if<abc::RegisterId>(input) : nullptr;
      if (!reg || *reg != *from || !PrivateCopyOutput(method, ast, i, end, *to, uses)) continue;
      auto output = method.ssa.Write({static_cast<std::uint32_t>(*producer)});
      const auto slot = output ? method.ssa.Slot(*output) : std::nullopt;
      if (!slot) continue;
      if (std::holds_alternative<analysis::Accumulator>(*slot))
        output = RetargetedOutput(method, ast, *producer, i, *from, uses);
      else if (std::get<abc::RegisterId>(*slot) != *from)
        continue;
      const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(i)});
      if (!output || output->value >= uses.size() || uses[output->value] != 1 ||
          reads.size() != 1 || reads.front().value != *output) continue;
      // Every intervening AST node is an independently discharged empty step.
      // The RHS, including any throw, remains at the producer; only its private
      // destination changes. Exact SSA uses include phi/exception observers.
      result->target = *to;
      tail.operation = analysis::NoOperation{};
      for (auto anchor = *producer + 1; anchor <= i; ++anchor)
        ast.statements[anchor].previous_origin = ast.statements[anchor - 1].pc;
      // The new target no longer denotes the producer's original saved SSA
      // definition. Do not reuse it as a candidate without a fresh proof.
      previous.reset();
    }
  }
}
}  // namespace abcd::source
