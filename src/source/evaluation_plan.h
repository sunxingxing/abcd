#ifndef ABCD_SOURCE_EVALUATION_PLAN_H_
#define ABCD_SOURCE_EVALUATION_PLAN_H_

#include <span>
#include "abcd/source/source_tree.h"
#include "phi_copies.h"
#include "literal_plan.h"
#include "cell_evaluation.h"

namespace abcd::source {
enum class EvaluationKind : std::uint8_t { kRemoved, kRoot, kInline, kInitializer, kEnvironment };
struct OpEvaluation {
  EvaluationKind kind = EvaluationKind::kRemoved;
  std::optional<analysis::UseId> consumer;
  std::optional<TemporaryReason> temporary;
};
// Conditional child-body/capture/write obligation, not a validated child.
// Sorted creation OpIds; distinct child owners are checked by AST ownership.
struct FunctionBinding {
  analysis::OpId creation;
  FunctionOwnerId child;
  // Parent mutable CellIds, in the child's incoming-cell order. Input/work
  // CaptureCells evidence must independently justify every edge at publication.
  std::vector<CellId> captures;
};
struct EvaluationPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<OpEvaluation> operations;
  // Phi input copies execute at incoming boundaries, outside expression trees.
  // This proves their values/schedules, not their eventual AST edge placement.
  std::optional<PhiCopies> phis;
  std::optional<LiteralPlan> literals;
  std::optional<CellEvaluation> cells;
  std::vector<FunctionBinding> functions;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class EvaluationError : std::uint8_t {
  kRevision, kInvalidGraph, kUnsupportedOperation, kProtocolOperand, kLivePhi,
  kOwnership, kOrder, kUnavailableValue, kWorkLimit, kResourceLimit
};
struct EvaluationShape {
  // Fixed prefix followed by argument occurrences. No array proportional to
  // range-call arity is allocated merely to describe their roles.
  std::span<const analysis::OperandRole> roles;
  bool expression;
  std::uint32_t arguments = 0;
  [[nodiscard]] std::uint64_t Count() const { return roles.size() + static_cast<std::uint64_t>(arguments); }
  [[nodiscard]] std::optional<analysis::OperandRole> Role(std::uint32_t index) const {
    if (index < roles.size()) return roles[index];
    if (index < Count()) return analysis::OperandRole::kArgument;
    return {};
  }
};
// Scalar JS order or staged invocation inputs (callee, raw receiver, args).
// Call syntax must separately prove reference binding and avoid direct eval;
// this forest alone does not authorize a member call or extra receiver read.
[[nodiscard]] std::optional<EvaluationShape> DescribeEvaluation(
    const analysis::ValueOperation& operation);
[[nodiscard]] bool OrderedEvaluation(const analysis::ValueOp& operation);
[[nodiscard]] bool BooleanEvaluation(const analysis::ValueProgram& graph,
                                     const analysis::ValueOperand& operand);
[[nodiscard]] bool PredicateSnapshot(const analysis::ValueProgram& graph,
    const analysis::ValueOp& consumer, const analysis::ValueOperand& operand);
// Requires verified normal IR. Produces a single-parent expression forest;
// a failed fusion keeps a snapshot, never duplicates or drops an effect.
// kConditional incoming cells retain external-binding obligations. It does not
// authorize standalone source publication. Supplying all FunctionBindings adds
// an explicit Language child-write premise; every child must discharge it.
[[nodiscard]] std::variant<EvaluationPlan, EvaluationError> BuildEvaluationPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {},
    IncomingCells incoming = IncomingCells::kReject,
    std::span<const FunctionBinding> functions = {});
// Iteratively expands the proposed forest in the declared input order, checking
// exact ordered occurrences and same-block availability. Call reference/source
// order still needs its separate certificate. No plan replay.
[[nodiscard]] std::variant<std::uint64_t, EvaluationError> VerifyEvaluationPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
