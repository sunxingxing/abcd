#ifndef ABCD_SOURCE_TREE_FLOW_H_
#define ABCD_SOURCE_TREE_FLOW_H_

#include "control_plan.h"
#include "evaluation_plan.h"
#include "original_flow.h"
#include "for_in_emission.h"
#include "for_of_emission.h"

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
struct TreeSelectionTruth {
  // Owns the retained input's source origin, independently of the raw test
  // that consumes it. A folded decision cannot borrow a later branch origin.
  analysis::OpId input_owner;
  analysis::ValueId value;
  ExprId input;
  analysis::UseId occurrence;
  std::optional<PureTruthTest> test;
};
using TreeForInEvents = OriginalEnumeration;
enum class EnumerationControl : std::uint8_t { kLoop, kExhausted };
struct ForOfTreeFlow {
  ForOfEmissionView view;
  std::vector<StmtId> statements;
  std::vector<std::optional<StmtId>> closes;
  std::vector<std::optional<EnumerationControl>> controls;
};
struct TreeFlow {
  std::vector<OriginalConcurrentNotification> notifications;
  // Independent typed protocol correspondence. Advance Commit and success-only
  // key assignment remain distinct; neither is an ordinary shared expression.
  std::vector<TreeForInEvents> enumerations;
  std::vector<std::optional<std::uint32_t>> forwarded_operations, forwarded_blocks;
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  // Indexed by OpId. Branches/returns map to their condition/returned value;
  // jumps and removed operations have no expression. Snapshot symbols come
  // from actual assignment targets, never from regenerated binding decisions.
  std::vector<std::optional<ExprId>> expressions;
  // Original ToBoolean/branch operations composed into one native logical node.
  std::vector<std::optional<TreeLogical>> logical_operations;
  // Independently proved pure truth equations consumed by a retained-value
  // logical selection; input retains its original saving assignment, if any,
  // after ordered prefixes.
  std::vector<std::optional<TreeSelectionTruth>> selection_truths;
  std::vector<std::uint8_t> conditional_operations;
  // A decision-selection root also produces a complete conditional value.
  // Keep its independently reconstructed scalar predicate separate, so the
  // original branch is checked without confusing it with the selected value.
  std::vector<std::optional<ExprId>> branch_predicates;
  // Derived from actual statement arms; expression verification checks the
  // corresponding original or complemented predicate without repeating reads.
  std::vector<BranchPolarity> branch_polarity;
  std::vector<std::optional<analysis::OpId>> edge_controls;
  std::vector<std::optional<StmtId>> statements;
  std::vector<std::optional<PropertyId>> initializers;
  // Actual original buffer prefix length per allocation. Constants verify the
  // count/keys; definitions may overwrite prefix positions in RHS order.
  std::vector<std::uint32_t> buffered_properties;
  std::vector<std::uint8_t> defined_properties; // PropertyId indexed, unique own write.
  std::vector<std::optional<SymbolId>> snapshots;
  // Written by the proof owning the actual snapshot/edge/catch assignment.
  // Recovery must not reconstruct raw definition identity from AST shape.
  std::vector<std::optional<OriginalSourceDefinition>> definitions;
  std::vector<std::optional<OriginalSourceDefinition>> declarations; // StmtId indexed.
  // OpId indexed, reconstructed from actual native roots. These values reuse
  // immutable invocation storage, not a source symbol or a derived VM entry.
  std::vector<ReceiverValue> receivers;
  // Indexed by CellId; recovered from actual allocation-site declarations.
  // These mutable storage identities are not SSA read snapshots.
  std::vector<std::optional<SymbolId>> cells;
  std::vector<OriginalCellBinding> cell_bindings; // Same verified CellId order.
  // Actual assignment targets, not regenerated BindingPlan identities.
  std::vector<std::optional<SymbolId>> phis;
  // Indexed by the freshly checked PhiCopies bundle ordinal.
  std::vector<TreeEdgeCopies> copies;
  // Phased mode only: recovered from actual catch clauses and assignment
  // targets, never copied from the builder's BindingPlan.
  std::vector<std::optional<SymbolId>> catch_parameters; // ValueId indexed.
  std::vector<std::optional<SymbolId>> catch_slots; // CatchSlotId indexed.
  std::vector<std::vector<ExprId>> catch_commits; // ExceptionCopies commit order.
  std::vector<OriginalExceptionRegion> exception_regions;
  std::vector<analysis::InactiveExceptionRegion> inactive_regions;
  std::vector<std::optional<StmtId>> tries; // Original TryId indexed.
  // Origins of independently checked synthetic loop/transfer statements.
  // Indexed by the synthetic ControlNode suffix, not inferred from text.
  std::vector<TreeOriginId> controls;
  std::vector<std::optional<ControlForward>> forwarded_controls;
  std::vector<std::uint8_t> switch_controls; // Freshly proved ports with no source statement.
  std::vector<std::optional<EnumerationControl>> enumeration_controls;
  ControlId first_control;
  std::vector<StmtId> control_statements; // Same synthetic suffix as controls.
  // Private boolean routing expressions checked by the completion projection.
  std::vector<ExprId> completion_expressions;
  std::optional<ExprId> hole_initializer;
  std::optional<ForOfTreeFlow> for_of;
  std::shared_ptr<const OriginalFlow> original;
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
    analysis::SemanticBudget budget = {}, TreeLocation* failure_location = nullptr);
// Checks the actual tree against freshly verified original exceptional control
// and staged copies. Same expression/ABI limitations as normal flow above.
// Exceptional indexing/traversal adds O(B + E + O + V + R + C) work/storage
// for blocks, edges, operations, values, regions and copy occurrences beyond
// the shared flow checks and prerequisites. Does not replay AST construction.
[[nodiscard]] std::variant<TreeFlow, TreeFlowError> VerifyExceptionTreeFlow(
    const analysis::ValueProgram& graph, const ExceptionTreeInput& exceptions,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    analysis::SemanticBudget budget = {},
    TreeControlCheck control_check = TreeControlCheck::kFresh, TreeLocation* failure_location = nullptr);
}  // namespace abcd::source
#endif
