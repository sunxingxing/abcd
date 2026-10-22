#ifndef ABCD_SOURCE_TREE_SEMANTICS_H_
#define ABCD_SOURCE_TREE_SEMANTICS_H_

#include "expression_forest.h"
#include "tree_flow.h"

namespace abcd::source {
enum class TreeSemanticError : std::uint8_t {
  kFlow, kOperand, kOperator, kConstant, kOrigin, kUnsupportedOperation,
  kResourceLimit, kWorkLimit, kDeleteStrictness, kSuperHomeObject, kSuperReceiver, kSuperKeyDomain, kNewTargetPremise, kNewTargetOwner, kThisPropertyScope, kSuperStoreOrder
};
struct TreeSemanticFailure {
  TreeSemanticError kind = TreeSemanticError::kFlow;
  std::variant<std::monostate, TreeFlowError> cause;
  TreeLocation location;
};
struct TreeSemantics {
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  // Same checked correspondence, retained without a second flow verification.
  TreeFlow flow;
};
// Conditional correspondence proof against already-verified input IR and the
// adapter's same-file resolved constants. Does not certify the supplied callable
// context or ABC identity/parameter census. Publication must bind those facts,
// this proof and an immutable tree together; this report alone cannot print.
// For an initialized-formal residual graph, this proves only the body. Native
// defaults and SignaturePlacement need VerifyDefaultSignature as well as ABI
// validation; tree structure alone is not prologue-migration permission.
// Conditional incoming-cell evaluation proves references to the child's
// explicit capture symbols, not those symbols' enclosing targets or spelling.
// A parent creation/family proof must close those obligations before printing.
[[nodiscard]] std::variant<TreeSemantics, TreeSemanticError> VerifyTreeSemantics(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    ExpressionConstants constants, analysis::SemanticBudget budget = {},
    TreeSemanticFailure* failure = nullptr);
// Same conditional correspondence contract, with original phase/region evidence.
// Does not authorize finally recognition, suspension lowering or publication.
[[nodiscard]] std::variant<TreeSemantics, TreeSemanticError> VerifyExceptionTreeSemantics(
    const analysis::ValueProgram& graph, const ExceptionTreeInput& exceptions,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    ExpressionConstants constants, analysis::SemanticBudget budget = {},
    TreeControlCheck control_check = TreeControlCheck::kFresh,
    TreeSemanticFailure* failure = nullptr);
}  // namespace abcd::source
#endif
