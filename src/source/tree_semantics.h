#ifndef ABCD_SOURCE_TREE_SEMANTICS_H_
#define ABCD_SOURCE_TREE_SEMANTICS_H_

#include "expression_forest.h"
#include "tree_flow.h"

namespace abcd::source {
enum class TreeSemanticError : std::uint8_t {
  kFlow, kOperand, kOperator, kConstant, kOrigin, kUnsupportedOperation,
  kResourceLimit, kWorkLimit
};
struct TreeSemantics {
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Conditional correspondence proof against already-verified input IR and the
// adapter's same-file resolved constants. Does not certify the supplied callable
// context or ABC identity/parameter census. Publication must bind those facts,
// this proof and an immutable tree together; this report alone cannot print.
// For an initialized-formal residual graph, this proves only the body. Native
// defaults and SignaturePlacement need VerifyDefaultSignature as well as ABI
// validation; tree structure alone is not prologue-migration permission.
[[nodiscard]] std::variant<TreeSemantics, TreeSemanticError> VerifyTreeSemantics(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    ExpressionConstants constants, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
