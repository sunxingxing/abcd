#include "call_results.h"

#include "expression_origins.h"

namespace abcd::source {
void ElideCallResults(const AstMethod& method, SourceAst& ast,
                     std::span<const std::size_t> uses) {
  if (method.ssa.IsPhased() || ast.regions.catches) return;
  const auto unused = [&](analysis::InstructionId call) {
    if (call.value >= method.instructions.size()) return false;
    const auto value = method.ssa.Write(call);
    if (!value || value->value >= uses.size() || uses[value->value]) return false;
    const auto slot = method.ssa.Slot(*value);
    return slot && std::holds_alternative<analysis::Accumulator>(*slot);
  };
  for (std::size_t i = 0; i < ast.statements.size(); ++i) {
    auto& statement = ast.statements[i];
    if (auto* staged = std::get_if<SourceStagedCall>(&statement.operation)) {
      // The node lives at the property PC but commits at the original call.
      // Never inspect the getter's SSA result to decide call-result liveness.
      if (unused(staged->call_statement)) staged->result = AssignmentResult::kUnused;
      continue;
    }
    auto* assignment = std::get_if<SourceAssignment>(&statement.operation);
    if (!assignment || assignment->result != AssignmentResult::kStored ||
        !std::holds_alternative<analysis::Accumulator>(assignment->target) ||
        !CanRetargetExpression(statement, assignment->value)) continue;
    if (!std::holds_alternative<CallExpression>(assignment->value) &&
        !std::holds_alternative<MemberCallExpression>(assignment->value) &&
        !std::holds_alternative<analysis::ConstructorCall>(assignment->value)) continue;
    if (unused({static_cast<std::uint32_t>(i)}))
      assignment->result = AssignmentResult::kUnused;
  }
}
}  // namespace abcd::source
