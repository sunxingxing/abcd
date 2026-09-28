#ifndef ABCD_SOURCE_FUNCTION_TREE_H_
#define ABCD_SOURCE_FUNCTION_TREE_H_

#include "control_plan.h"
#include "expression_forest.h"
#include "function_syntax.h"
#include "capture_tree.h"

namespace abcd::source {
struct ConstructedFunction {
  FunctionTree tree;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class FunctionTreeError : std::uint8_t {
  kControl, kExpression, kBinding, kName, kOrigin, kOwnership,
  kResourceLimit, kWorkLimit, kStructure, kSemantics, kCapture
};
// Builds syntax checked against IR/control/evaluation and resolved constants.
// Caller establishes callable/strictness from creation evidence; construction
// does not certify those facts or authorize publishing.
// Existing graph/plans/constants are caller-owned budget; syntax and all
// construction-owned output/scratch share this call's byte/work allowance.
// An incoming creation derives capture aliases from the actual checked parent
// tree and verifies the resulting child relationship. Caller-supplied captures
// must be empty in this mode. This does not close transitive family premises.
// AllowPartial currently retains closed expression roots before an unsupported
// lowering in one normal straight-line region without cells/phis/children.
// The suffix becomes an explicit gap; malformed inputs remain errors. Other
// topologies retain complete-only behavior, never a guessed linearization.
[[nodiscard]] std::variant<ConstructedFunction, FunctionTreeError>
BuildFunctionTree(const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, FunctionSyntax syntax,
    ExpressionConstants constants, analysis::SemanticBudget budget = {},
    const IncomingCaptureTree* incoming = nullptr,
    RecoveryMode recovery = RecoveryMode::kCompleteOnly);
}  // namespace abcd::source
#endif
