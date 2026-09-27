#ifndef ABCD_SOURCE_FUNCTION_TREE_H_
#define ABCD_SOURCE_FUNCTION_TREE_H_

#include "control_plan.h"
#include "expression_forest.h"
#include "function_syntax.h"

namespace abcd::source {
struct ConstructedFunction {
  FunctionTree tree;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class FunctionTreeError : std::uint8_t {
  kControl, kExpression, kBinding, kName, kOrigin, kOwnership,
  kResourceLimit, kWorkLimit, kStructure, kSemantics
};
// Builds syntax checked against IR/control/evaluation and resolved constants.
// Caller establishes callable/strictness from creation evidence; construction
// does not certify those facts or authorize publishing.
// Existing graph/plans/constants are caller-owned budget; syntax and all
// construction-owned output/scratch share this call's byte/work allowance.
[[nodiscard]] std::variant<ConstructedFunction, FunctionTreeError>
BuildFunctionTree(const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, FunctionSyntax syntax,
    ExpressionConstants constants, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
