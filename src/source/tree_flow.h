#ifndef ABCD_SOURCE_TREE_FLOW_H_
#define ABCD_SOURCE_TREE_FLOW_H_

#include "control_plan.h"
#include "evaluation_plan.h"

namespace abcd::source {
enum class TreeFlowError : std::uint8_t {
  kRevision, kStructure, kControl, kEvaluation, kPlacement, kSnapshot,
  kSequence, kResourceLimit, kWorkLimit
};
struct TreeEdgeCopies {
  analysis::IncomingKey incoming;
  std::vector<ExprId> expressions;
  std::vector<std::optional<SymbolId>> temporaries;
};
struct TreeFlow {
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  // Indexed by OpId. Branches/returns map to their condition/returned value;
  // jumps and removed operations have no expression. Snapshot symbols come
  // from actual assignment targets, never from regenerated binding decisions.
  std::vector<std::optional<ExprId>> expressions;
  std::vector<std::optional<StmtId>> statements;
  std::vector<std::optional<PropertyId>> initializers;
  std::vector<std::optional<SymbolId>> snapshots;
  // Actual assignment targets, not regenerated BindingPlan identities.
  std::vector<std::optional<SymbolId>> phis;
  // Indexed by the freshly checked PhiCopies bundle ordinal.
  std::vector<TreeEdgeCopies> copies;
  // Origins of independently checked synthetic loop/transfer statements.
  // Indexed by the synthetic ControlNode suffix, not inferred from text.
  std::vector<TreeOriginId> controls;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Requires admitted/verified input IR. Checks actual tree sequences and arm
// ownership against normal ControlPlan and EvaluationPlan. Does not yet prove
// expression operands/operators, input constants, or original-PC fidelity.
// Counts tree storage and all owned verification output/scratch in budget;
// caller separately admits input graph/control/evaluation storage.
[[nodiscard]] std::variant<TreeFlow, TreeFlowError> VerifyTreeFlow(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
