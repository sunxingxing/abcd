#ifndef ABCD_SOURCE_TREE_FLOW_H_
#define ABCD_SOURCE_TREE_FLOW_H_

#include "control_plan.h"
#include "evaluation_plan.h"

namespace abcd::source {
struct ExceptionTreeInput;
// Reuse is confined to one synchronous construction: the builder has checked
// this exact control input and has not modified it. Independent callers must
// retain kFresh; this does not skip checking the tree or evaluation plan.
enum class TreeControlCheck : std::uint8_t { kFresh, kReuseInBuild };
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
  // A native logical/conditional expression selects this value without a copy statement.
  std::optional<ExprId> selected;
};
struct TreeFlow {
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  // Indexed by OpId. Branches/returns map to their condition/returned value;
  // jumps and removed operations have no expression. Snapshot symbols come
  // from actual assignment targets, never from regenerated binding decisions.
  std::vector<std::optional<ExprId>> expressions;
  // Original ToBoolean/branch operations composed into one native logical node.
  std::vector<std::optional<TreeLogical>> logical_operations;
  std::vector<std::uint8_t> conditional_operations;
  // A decision-selection root also produces a complete conditional value.
  // Keep its independently reconstructed scalar predicate separate, so the
  // original branch is checked without confusing it with the selected value.
  std::vector<std::optional<ExprId>> branch_predicates;
  // Derived from actual statement arms; expression verification checks the
  // corresponding original or complemented predicate without repeating reads.
  std::vector<BranchPolarity> branch_polarity;
  std::vector<std::optional<StmtId>> statements;
  std::vector<std::optional<PropertyId>> initializers;
  // Actual original buffer prefix length per allocation. Constants verify the
  // count/keys; definitions may overwrite prefix positions in RHS order.
  std::vector<std::uint32_t> buffered_properties;
  std::vector<std::uint8_t> defined_properties; // PropertyId indexed, unique own write.
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
  ControlId first_control;
  std::vector<StmtId> control_statements; // Same synthetic suffix as controls.
  // Private boolean routing expressions checked by the completion projection.
  std::vector<ExprId> completion_expressions;
  std::optional<ExprId> hole_initializer;
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
    analysis::SemanticBudget budget = {},
    TreeControlCheck control_check = TreeControlCheck::kFresh);
}  // namespace abcd::source
#endif
