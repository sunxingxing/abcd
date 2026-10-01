#ifndef ABCD_SOURCE_LOGICAL_REGIONS_H_
#define ABCD_SOURCE_LOGICAL_REGIONS_H_

#include "control_plan.h"
#include "abcd/source/source_tree.h"

namespace abcd::source {
class TreeCheckBudget;
struct EvaluationPlan;
// A value-selection diamond, not permission to move effects across its header.
// The expression planner must still own the RHS subtree, remove its phi copies,
// and preserve the order of every surrounding call/reference before emission.
struct SelectionChild { analysis::OpId branch; };
struct SelectionCondition {
  analysis::OpId branch;
  bool shared_on_taken;
};
using SelectionOperand = std::variant<analysis::ValueId, analysis::SpecialValue,
    analysis::IntegerLiteral, analysis::Float64Bits, SelectionChild>;
inline SelectionOperand SelectionLeaf(const analysis::ValueOperand& value) {
  return std::visit([](const auto& input) -> SelectionOperand { return input; }, value);
}
struct SelectionRegion {
  TreeLogical operation;
  analysis::OpId boolean;
  analysis::OpId branch;
  analysis::ValueId merged;
  analysis::ValueId left;
  SelectionOperand right;
  analysis::WorkBlockId header;
  std::optional<analysis::WorkBlockId> keep;
  analysis::WorkBlockId evaluate;
  analysis::WorkBlockId join;
  std::optional<analysis::EdgeId> keep_input;
  std::optional<analysis::EdgeId> evaluate_input;
  // Present for a true/false value selection; left is its actual predicate.
  std::optional<SelectionOperand> consequent;
  // Conditional arms may start with empty jump corridors. Their final blocks
  // remain keep/evaluate. A subset of a shared phi must stay an assignment at
  // this branch, never replace the phi's other incoming values.
  std::optional<ControlId> keep_path;
  std::optional<ControlId> evaluate_path;
  bool complete_phi = true;
  // Exactly one containing selection may own this branch as an expression.
  std::optional<analysis::OpId> parent;
  // Latest nonconstant dependency needed before a fused join consumer.
  std::optional<analysis::OpId> last_dependency;
  // A shared-arm decision retains every original scalar predicate and its
  // conditional evaluation path. These are disjoint, flat ownership lists,
  // never a copied CFG or an unconditional evaluation of the RHS.
  std::vector<SelectionCondition> conditions;
  std::vector<ControlId> condition_blocks;
};
inline bool IsSelectionConstant(const analysis::ValueProgram& graph, analysis::OpId id) {
  if (id.value >= graph.ops.size()) return false;
  const auto& op = graph.ops[id.value];
  return op.life == analysis::ValueLife::kLive && std::holds_alternative<analysis::ValueCopy>(op.operation) &&
      op.result && op.use_count == 1 && op.first_use.value < graph.uses.size() &&
      !std::holds_alternative<analysis::ValueId>(graph.uses[op.first_use.value].operand) &&
      op.normal == analysis::LanguageEffect::kNone && op.before_throw == analysis::LanguageEffect::kNone &&
      op.exceptional == analysis::ExceptionalCompletion::kImpossible;
}
// Requires verified simplified normal IR and its verified ControlPlan. Finds
// ToBoolean diamonds with a single result phi, including an empty keep arm.
// O(operations + values + uses + control nodes), with indexed block summaries;
// never scans the whole graph separately for each branch or phi.
[[nodiscard]] std::vector<SelectionRegion> FindLogicalRegions(
    const analysis::ValueProgram& graph, const ControlPlan& control, TreeCheckBudget* budget = nullptr);
// Select short-circuit regions or two-arm conditional regions. Every selected
// arm must be one owned evaluation subtree; the predicate remains at its site.
[[nodiscard]] std::vector<SelectionRegion> FindSelectionExpressions(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, TreeCheckBudget& budget);
// Indexed eligible LHS producers; only the exact predicate/keep-edge pair is
// projected. Requires checked value uses and base evaluation placements. Each
// producer stays in its header, across no observable event/environment change.
[[nodiscard]] std::vector<std::optional<analysis::UseId>> FindLogicalLeftInputs(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation, TreeCheckBudget& budget);
}  // namespace abcd::source
#endif
