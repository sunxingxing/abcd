#ifndef ABCD_SOURCE_TREE_FLOW_H_
#define ABCD_SOURCE_TREE_FLOW_H_

#include "control_plan.h"
#include "evaluation_plan.h"

namespace abcd::source {
struct ExceptionTreeInput;
enum class TreeFlowError : std::uint8_t {
  kRevision, kStructure, kControl, kEvaluation, kPlacement, kSnapshot,
  kSequence, kResourceLimit, kWorkLimit
};
struct TreeEdgeCopies {
  analysis::IncomingKey incoming;
  // Normal bundles follow ParallelSchedule. Exceptional bundles follow the
  // original PhiBundle copy order and target private staging slots instead.
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
  // Actual buffered prefix length per allocation. Own-write initializers are
  // a separately checked suffix; constants verify this prefix independently.
  std::vector<std::uint32_t> buffered_properties;
  std::vector<std::optional<SymbolId>> snapshots;
  // Indexed by CellId; recovered from actual allocation-site declarations.
  // These mutable storage identities are not SSA read snapshots.
  std::vector<std::optional<SymbolId>> cells;
  // Actual assignment targets, not regenerated BindingPlan identities.
  std::vector<std::optional<SymbolId>> phis;
  // Indexed by the freshly checked PhiCopies bundle ordinal.
  std::vector<TreeEdgeCopies> copies;
  // Phased mode only: recovered from actual catch clauses and assignment
  // targets, never copied from the builder's BindingPlan.
  std::vector<std::optional<SymbolId>> catch_parameters; // ValueId indexed.
  std::vector<std::optional<SymbolId>> catch_slots; // CatchSlotId indexed.
  std::vector<std::vector<ExprId>> catch_commits; // ExceptionCopies commit order.
  std::vector<std::optional<StmtId>> tries; // Original TryId indexed.
  // Origins of independently checked synthetic loop/transfer statements.
  // Indexed by the synthetic ControlNode suffix, not inferred from text.
  std::vector<TreeOriginId> controls;
  // Private boolean routing expressions checked by the completion projection.
  std::vector<ExprId> completion_expressions;
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
// Checks the actual tree against freshly verified original exceptional control
// and staged copies. Same expression/ABI limitations as normal flow above.
// Exceptional indexing/traversal adds O(B + E + O + V + R + C) work/storage
// for blocks, edges, operations, values, regions and copy occurrences beyond
// the shared flow checks and prerequisites. Does not replay AST construction.
[[nodiscard]] std::variant<TreeFlow, TreeFlowError> VerifyExceptionTreeFlow(
    const analysis::ValueProgram& graph, const ExceptionTreeInput& exceptions,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
