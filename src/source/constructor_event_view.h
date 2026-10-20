#ifndef ABCD_SOURCE_CONSTRUCTOR_EVENT_VIEW_H_
#define ABCD_SOURCE_CONSTRUCTOR_EVENT_VIEW_H_
#include "evaluation_plan.h"
#include "tree_checks.h"
namespace abcd::source {
// This view accounts for the complete existing constructor/argument forest.
// It grants no relocation: the selection's original edges and the common
// schedule, catch and Reference verifiers still own conditional execution.
enum class ConstructorEventRole : std::uint8_t {
  kInvocation,
  kLiteral,
  kOperand,
  kGuard
};
struct ConstructorEvent {
  analysis::OpId operation, constructor;
  ConstructorEventRole role;
  std::optional<analysis::OpId> literal;
  std::optional<analysis::UseId> consumer;
  std::optional<analysis::WorkBlockId> commit;
  std::optional<std::size_t> exception;
  std::optional<analysis::ValueHandler> handler;
  bool operator==(const ConstructorEvent&) const = default;
};
struct ConstructorInput {
  analysis::UseId occurrence;
  analysis::OpId constructor;
  analysis::OperandRole role;
  analysis::ValueOperand value;
};
struct ConstructorEventView {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<std::optional<ConstructorEvent>> events;
  std::vector<std::optional<analysis::UseId>> outputs;
  std::vector<std::optional<ConstructorInput>> inputs;
};
enum class ConstructorEventError : std::uint8_t {
  kRevision,
  kCoverage,
  kCorrespondence
};
// Requires independently verified LiteralPlan and evaluation placements.
// O(I+U), using one functional-parent index; never a subtree scan per nested
// literal.
[[nodiscard]] ConstructorEventView BuildConstructorEventView(
    const analysis::ValueProgram&, const EvaluationPlan&, TreeCheckBudget&);
void VerifyConstructorEventView(const analysis::ValueProgram&,
                                const EvaluationPlan&,
                                const ConstructorEventView&, TreeCheckBudget&);
}  // namespace abcd::source
#endif
