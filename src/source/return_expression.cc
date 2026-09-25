#include "return_expression.h"

#include "expression_origins.h"
#include "initialized_guard.h"

namespace abcd::source {
namespace {
bool HasMergedOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.class_origin || statement.previous_origin || statement.literal_origin;
}
void FoldCallReturns(const AstMethod& method, SourceAst& ast,
                     std::span<const std::size_t> uses) {
  if (method.ssa.IsPhased() || ast.regions.catches) return;
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    if (block.instruction_count < 2) continue;
    auto& tail = ast.statements[end - 1];
    const auto* returned = std::get_if<SourceReturn>(&tail.operation);
    if (!returned || !std::holds_alternative<analysis::Accumulator>(returned->value) ||
        HasMergedOrigin(tail)) continue;
    // One scan per block; no backwards suffix search for every call owner.
    for (std::size_t i = block.first_instruction; i + 1 < end; ++i) {
      auto& head = ast.statements[i];
      auto* call = std::get_if<SourceStagedCall>(&head.operation);
      if (!call || call->return_statement || call->stored_result ||
          call->result != AssignmentResult::kStored ||
          HasMergedOrigin(head) || call->call_statement.value != end - 2 ||
          call->call_statement.value <= i) continue;
      const auto& anchor = ast.statements[call->call_statement.value];
      if (!std::holds_alternative<analysis::NoOperation>(anchor.operation) ||
          HasMergedOrigin(anchor)) continue;
      const auto output = method.ssa.Write(call->call_statement);
      const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(end - 1)});
      if (!output || output->value >= uses.size() || uses[output->value] != 1 ||
          reads.size() != 1 || reads.front().value != *output) continue;
      // The Reference/argument owner stays intact; only its unobserved result
      // commit becomes a return after invocation. Getter/argument effects stay.
      call->return_statement = analysis::InstructionId{static_cast<std::uint32_t>(end - 1)};
      tail.operation = analysis::NoOperation{};
      tail.previous_origin = anchor.pc;
      break;
    }
  }
}
}  // namespace

void FoldReturnExpressions(const AstMethod& method, SourceAst& ast,
                           std::span<const std::size_t> uses) {
  FoldCallReturns(method, ast, uses);
  for (const auto& block : method.flow.blocks) {
    if (block.instruction_count < 2) continue;
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    const auto returned = end - 1;
    auto defined = returned - 1;
    while (defined > block.first_instruction && IsElidedInitializedGuard(method, ast, defined))
      --defined;
    auto& tail = ast.statements[returned];
    auto& head = ast.statements[defined];
    const auto* result = std::get_if<SourceReturn>(&tail.operation);
    auto* assignment = std::get_if<SourceAssignment>(&head.operation);
    if (!result || !assignment ||
        assignment->result != AssignmentResult::kStored || assignment->object_field ||
        !std::holds_alternative<analysis::Accumulator>(result->value) ||
        !std::holds_alternative<analysis::Accumulator>(assignment->target) ||
        !CanRetargetExpression(head, assignment->value) || HasMergedOrigin(tail)) continue;
    const auto value = method.ssa.Write({static_cast<std::uint32_t>(defined)});
    const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(returned)});
    if (!value || value->value >= uses.size() || reads.size() != 1 ||
        reads.front().value != *value) continue;
    bool guarded = true;
    std::size_t consumers = reads.size();
    for (auto i = defined + 1; i < returned; ++i) {
      const auto inputs = method.ssa.Reads({static_cast<std::uint32_t>(i)});
      if (inputs.size() != 1 || inputs.front().value != *value) { guarded = false; break; }
      consumers += inputs.size();
    }
    if (!guarded || uses[value->value] != consumers) continue;
    // Keep the RHS at its creation/effect PC. Removing the private acc commit
    // is valid only with no observer except this return and already-proved
    // redundant guards. Exact use counts retain all normal/exceptional phis.
    // An already-proved member call keeps every fused origin on this statement;
    // the printer relocates those origins to the complete return-call span.
    auto expression = std::move(assignment->value);
    head.operation = SourceReturnExpression{std::move(expression)};
    tail.operation = analysis::NoOperation{};
    // Chain adjacent origins so every guard and return PC shares the actual
    // emitted return expression without reverse-engineering generated text.
    for (auto i = defined + 1; i <= returned; ++i)
      ast.statements[i].previous_origin = ast.statements[i - 1].pc;
  }
}
}  // namespace abcd::source
