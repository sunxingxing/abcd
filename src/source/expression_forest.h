#ifndef ABCD_SOURCE_EXPRESSION_FOREST_H_
#define ABCD_SOURCE_EXPRESSION_FOREST_H_

#include "binding_plan.h"
#include "call_plan.h"

namespace abcd::source {
// Constants are resolved by the ABC adapter before syntax construction. Pool
// indices never become executable tree operands. Entries are indexed by OpId.
using ExpressionConstant = std::variant<std::monostate, NameId, TextId>;
struct ExpressionConstants {
  std::span<const ExpressionConstant> operations;
  std::size_t name_count = 0;
  std::size_t text_count = 0;
  std::optional<TextId> number_type_text;
};
struct ExpressionForest {
  BindingPlan bindings;
  CallPlan calls;
  std::vector<ExprNode> expressions;
  std::vector<ExprId> arguments;
  std::vector<TreeOrigin> origins;
  std::vector<TreeOriginId> origin_parts;
  // A branch maps to its condition; a return maps to its returned expression.
  // Jumps and removed operations have no expression. Statement assembly owns
  // control-operation placements, not this expression-only construction step.
  std::vector<std::optional<ExprId>> operations;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ExpressionError : std::uint8_t {
  kBinding, kOrigin, kConstant, kUnavailableValue, kUnsupportedOperation,
  kOwnership, kResourceLimit, kWorkLimit
};
// Inputs must have passed import/rewrite verification. Builds each inline tree
// once, in operation order, without recursion or sharing effectful expressions.
// Output is NOT a frozen/publication-ready tree; scope/control/source proofs
// still have to be checked after statement assembly.
[[nodiscard]] std::variant<ExpressionForest, ExpressionError>
BuildExpressionForest(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, std::uint32_t parameter_count,
    FunctionOwnerId owner, ExpressionConstants constants,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
