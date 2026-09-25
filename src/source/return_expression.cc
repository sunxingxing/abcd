#include "return_expression.h"

#include "expression_origins.h"

namespace abcd::source {
namespace {
bool HasMergedOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.class_origin || statement.previous_origin;
}
}  // namespace

void FoldReturnExpressions(const AstMethod& method, SourceAst& ast,
                           std::span<const std::size_t> uses) {
  for (const auto& block : method.flow.blocks) {
    if (block.instruction_count < 2) continue;
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    const auto returned = end - 1;
    const auto defined = returned - 1;
    auto& tail = ast.statements[returned];
    auto& head = ast.statements[defined];
    const auto* result = std::get_if<SourceReturn>(&tail.operation);
    auto* assignment = std::get_if<SourceAssignment>(&head.operation);
    if (!result || !assignment ||
        !std::holds_alternative<analysis::Accumulator>(result->value) ||
        !std::holds_alternative<analysis::Accumulator>(assignment->target) ||
        !CanRetargetExpression(head, assignment->value) || HasMergedOrigin(tail)) continue;
    const auto value = method.ssa.Write({static_cast<std::uint32_t>(defined)});
    const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(returned)});
    if (!value || uses[value->value] != 1 || reads.size() != 1 ||
        reads.front().value != *value) continue;
    // Keep the RHS at its creation/effect PC. Removing the private acc commit
    // is valid only with no normal or exceptional observer except this return.
    // An already-proved member call keeps every fused origin on this statement;
    // the printer relocates those origins to the complete return-call span.
    auto expression = std::move(assignment->value);
    head.operation = SourceReturnExpression{std::move(expression)};
    tail.operation = analysis::NoOperation{};
    tail.previous_origin = head.pc;
  }
}
}  // namespace abcd::source
