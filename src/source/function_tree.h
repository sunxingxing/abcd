#ifndef ABCD_SOURCE_FUNCTION_TREE_H_
#define ABCD_SOURCE_FUNCTION_TREE_H_

#include "control_plan.h"
#include "expression_forest.h"

namespace abcd::source {
struct ParameterSyntax {
  std::optional<NameId> name;
  NameEvidence evidence = NameEvidence::kSynthesized;
};
struct FunctionSyntax {
  RouteInputIdentity identity;
  abc::EntityOffset method;
  FunctionOwnerId owner;
  CallableKind callable;
  Strictness strictness;
  std::vector<ParameterSyntax> parameters;
  std::vector<std::u16string> names;
  std::vector<std::u16string> texts;
};
struct ConstructedFunction {
  FunctionTree tree;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class FunctionTreeError : std::uint8_t {
  kControl, kExpression, kBinding, kName, kOrigin, kOwnership,
  kResourceLimit, kWorkLimit, kStructure
};
// Builds structurally checked syntax only. Caller establishes callable/strictness from creation
// evidence; construction does not certify those facts or authorize publishing.
// Existing graph/plans/constants are caller-owned budget; syntax and all
// construction-owned output/scratch share this call's byte/work allowance.
[[nodiscard]] std::variant<ConstructedFunction, FunctionTreeError>
BuildFunctionTree(const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, FunctionSyntax syntax,
    ExpressionConstants constants, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
