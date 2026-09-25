#include "unused_results.h"

#include <type_traits>

#include "expression_origins.h"

namespace abcd::source {
namespace {
bool HasStandaloneValue(const SourceExpression& expression) {
  return std::visit([](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, analysis::ScalarExpression> ||
                  std::is_same_v<T, MemberExpression> ||
                  std::is_same_v<T, StringComparison> ||
                  std::is_same_v<T, ModuleExpression> ||
                  std::is_same_v<T, GlobalExpression> ||
                  std::is_same_v<T, analysis::GlobalObjectLoad> ||
                  std::is_same_v<T, LocalExpression> ||
                  std::is_same_v<T, CapturedExpression> ||
                  std::is_same_v<T, analysis::EmptyObjectCreation> ||
                  std::is_same_v<T, analysis::BufferedObjectCreation> ||
                  std::is_same_v<T, analysis::BufferedArrayCreation> ||
                  std::is_same_v<T, CallExpression> ||
                  std::is_same_v<T, MemberCallExpression> ||
                  std::is_same_v<T, analysis::ConstructorCall>) {
      // Keep every getter, coercion, allocation, binding read and exception
      // at its original PC. These writers emit legal expression statements.
      return true;
    } else {
      // Bare string statements can become directives. Creation naming and
      // super/rest/await owner protocols require independent admission.
      static_assert(std::is_same_v<T, StringExpression> ||
                    std::is_same_v<T, FunctionExpression> ||
                    std::is_same_v<T, ClassExpression> ||
                    std::is_same_v<T, MethodExpression> ||
                    std::is_same_v<T, AccessorExpression> ||
                    std::is_same_v<T, analysis::SuperRange> ||
                    std::is_same_v<T, analysis::RestCopy> ||
                    std::is_same_v<T, analysis::AwaitValue>);
      return false;
    }
  }, expression);
}
}  // namespace
void ElideUnusedResults(const AstMethod& method, SourceAst& ast,
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
      if (!staged->return_statement && unused(staged->call_statement))
        staged->result = AssignmentResult::kUnused;
      continue;
    }
    auto* assignment = std::get_if<SourceAssignment>(&statement.operation);
    if (!assignment || assignment->result != AssignmentResult::kStored ||
        !std::holds_alternative<analysis::Accumulator>(assignment->target) ||
        !CanRetargetExpression(statement, assignment->value)) continue;
    if (!HasStandaloneValue(assignment->value)) continue;
    if (unused({static_cast<std::uint32_t>(i)}))
      assignment->result = AssignmentResult::kUnused;
  }
}
}  // namespace abcd::source
