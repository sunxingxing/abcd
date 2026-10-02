#ifndef ABCD_SOURCE_EVALUATION_PLAN_H_
#define ABCD_SOURCE_EVALUATION_PLAN_H_

#include <span>
#include "abcd/source/source_tree.h"
#include "phi_copies.h"
#include "exception_copies.h"
#include "literal_plan.h"
#include "cell_evaluation.h"
#include "module_guards.h"
#include "hole_protocol.h"
#include "class_initializers.h"

namespace abcd::source {
enum class BranchPolarity : std::uint8_t { kTaken, kFallthrough };
// kPhiInput owns a sole incoming-copy RHS at the same block exit. It is
// neither a value snapshot nor permission to cross a branch/exception edge.
// kLogicalLeft owns one native logical LHS: its original boolean use and
// retained phi input are two observations of that single expression result.
// kLogicalValue owns a logical/conditional result at one join consumer; logical_values
// identifies the eliminated phi location. Its cross-region inputs need interval proof.
enum class EvaluationKind : std::uint8_t {
  kRemoved, kRoot, kInline, kInitializer, kEnvironment, kModuleGuard, kElidedPure, kAlias, kNestedLiteral, kClassProjection, kReceiverGuard, kCellGuard, kPhiInput, kLogicalLeft, kLogicalValue, kPredicateSnapshot
};
enum class EvaluationFlow : std::uint8_t { kNormal, kPhased };
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
// Conditional source-module obligation. Original ModuleGuards and metadata
// binding must independently justify each entry before publishing any source.
// A guarded pair, or a parent-proven initialized load without a guard, becomes
// one throwing live-binding read. Absence never removes an original Hole check.
struct ModuleEvaluation {
  ModuleReadSite pair;
  analysis::ValueModuleRead read;
  ModuleSymbol target;
};
enum class NaNProjection : std::uint8_t { kExact, kIntegerCoercion };
struct EvaluationPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<OpEvaluation> operations;
  // Ordinary phi copies execute at incoming boundaries. Selected logical_values
  // consume both inputs in one expression instead; this table still proves
  // original values/schedules, not their eventual AST edge placement.
  std::optional<PhiCopies> phis;
  std::optional<LiteralPlan> literals;
  std::optional<CellEvaluation> cells;
  std::vector<FunctionBinding> functions;
  std::vector<ModuleEvaluation> modules;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  // Invocation count/tail obligation, not a native JS arguments binding.
  std::optional<analysis::ArgumentFrameFact> arguments;
  EvaluationFlow flow = EvaluationFlow::kNormal;
  std::optional<ExceptionCopies> exceptions;
  // Flat, independently checked copy identities. Empty in the normal route,
  // whose IR is already simplified. Never aliases a phi or catch binding.
  std::vector<analysis::ValueId> aliases;
  std::optional<HoleProtocol> holes;
  std::vector<ClassInitializerCall> class_initializers;
  ClassProjection class_projection;
  std::vector<ClassDefinitionSite> class_definitions;
  analysis::ClosureWrites closure_writes = analysis::ClosureWrites::kUnknown;
  // Conditional protocol obligation: the same derived owner, or actual capture
  // edges to its preserved receiver cells. The family factory discharges this
  // against unchanged input/work pairs after default recovery, never Language alone.
  std::vector<analysis::OpId> receiver_guards;
  // Phi values exclusively owned by native logical or conditional expressions, indexed by
  // ValueId. Each entry points to its original conditional branch operation.
  std::vector<std::optional<analysis::OpId>> logical_values;
  // Selected buffered allocations own a chain of staged definitions across selections.
  // Indexed by allocation, pointing to the last definition. Original local snapshots
  // remain independently checked.
  std::vector<std::optional<analysis::OpId>> logical_objects;
  std::vector<NaNProjection> integer_nans;
};
inline analysis::ValueId ResolveEvaluationValue(const EvaluationPlan& plan, analysis::ValueId value) {
  return plan.aliases.empty() ? value : plan.aliases[value.value];
}
// After VerifyEvaluationPlan: normal Hole transport is closed over copies and
// absent class heritage only. It has source positions, but no JS value/binding.
inline bool IsAbsentHeritageTransport(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, const analysis::ValueOp& op) {
  return plan.flow == EvaluationFlow::kNormal && plan.holes && op.result &&
      op.normal == analysis::LanguageEffect::kNone && op.before_throw == analysis::LanguageEffect::kNone &&
      op.exceptional == analysis::ExceptionalCompletion::kImpossible &&
      graph.values[op.result->value].domain == analysis::ValueKinds::Hole() &&
      (std::holds_alternative<analysis::ValueHole>(op.operation) ||
       std::holds_alternative<analysis::ValueCopy>(op.operation));
}
inline bool NeedsHoleToken(const EvaluationPlan& plan) {
  return plan.holes && plan.flow == EvaluationFlow::kPhased;
}
inline analysis::ValueOperand ResolveEvaluationOperand(
    const EvaluationPlan& plan, const analysis::ValueOperand& operand) {
  if (const auto* value = std::get_if<analysis::ValueId>(&operand)) return ResolveEvaluationValue(plan, *value);
  return operand;
}
enum class EvaluationError : std::uint8_t {
  kRevision, kInvalidGraph, kUnsupportedOperation, kProtocolOperand, kLivePhi,
  kOwnership, kOrder, kUnavailableValue, kWorkLimit, kResourceLimit
};
enum class ReferenceBoundary : std::uint8_t { kNone, kBeforeStoredValue };
// ConditionalArgument reads a declared slot only when ordinal < actual count.
// A later AST must bind that count and the extra tail to this invocation; this
// scheduling mode never authorizes an eager call-argument array or rest rewrite.
// ClassHeritage preserves the class-creation protocol occurrence and evaluates
// language heritage in order; definite Hole means absent extends. Scheduling
// does not establish constructor/member bodies, captures or inheritance syntax.
// ConstructorGuard retains the VM check as an ordered event and snapshots its
// input. It does not authorize guard elision or reading uninitialized JS this.
enum class InputEvaluation : std::uint8_t { kEager, kConditionalArgument, kClassHeritage, kReceiverGuard };
struct EvaluationShape {
  // Fixed prefix followed by argument occurrences. No array proportional to
  // range-call arity is allocated merely to describe their roles.
  std::span<const analysis::OperandRole> roles;
  bool expression;
  std::uint32_t arguments = 0;
  // An internal source-language evaluation boundary, not another CFG edge.
  // Precomputed RHS values cannot be fused across reference/key conversion.
  ReferenceBoundary reference = ReferenceBoundary::kNone;
  InputEvaluation inputs = InputEvaluation::kEager;
  [[nodiscard]] bool NeedsSnapshot(analysis::OperandRole role) const {
    return inputs == InputEvaluation::kConditionalArgument || inputs == InputEvaluation::kReceiverGuard ||
        (reference == ReferenceBoundary::kBeforeStoredValue &&
         role == analysis::OperandRole::kStoredValue);
  }
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
// Constant-time semantic checks. Conditional slot uses remain protocol reads:
// their producers execute at the original point, never inside a count predicate.
[[nodiscard]] std::optional<EvaluationError> CheckArgumentSnapshot(
    const analysis::ValueProgram& graph, const analysis::ValueOp& operation);
[[nodiscard]] std::optional<EvaluationError> CheckEvaluationUse(
    const analysis::ValueProgram& graph, const analysis::OperandUse& use,
    const EvaluationShape& shape, std::uint32_t ordinal);
[[nodiscard]] bool BooleanEvaluation(const analysis::ValueProgram& graph,
                                     const analysis::ValueOperand& operand);
// Identity conversion requires an existing domain proof, never a source name
// or apparent loop-counter shape. Arbitrary Language includes BigInt/coercion.
[[nodiscard]] bool LegacyNegationEvaluation(const analysis::ValueProgram& graph,
                                          const analysis::ValueOperand& operand);
[[nodiscard]] bool NumberEvaluation(const analysis::ValueProgram& graph, const analysis::ValueOperand& operand);
[[nodiscard]] bool NonNaNNumberEvaluation(const analysis::ValueProgram& graph,
                                        const analysis::ValueOperand& operand);
// An unproved ToNumeric input uses exclusive scratch for one conversion.
[[nodiscard]] bool NeedsNumericScratch(const analysis::ValueProgram&, const analysis::ValueOp&);
[[nodiscard]] bool PredicateSnapshot(const analysis::ValueProgram& graph,
    const analysis::ValueOp& consumer, const analysis::ValueOperand& operand);
// Requires verified normal IR. Produces a single-parent expression forest;
// a failed fusion keeps a snapshot, never duplicates or drops an effect.
// kConditional incoming cells retain external-binding obligations. It does not
// authorize standalone source publication. Supplying all FunctionBindings adds
// an explicit Language child-write premise; every child must discharge it.
// A non-Unknown closure_writes premise requires complete family write validation,
// including primitive buffered objects without actual child functions.
// Family demands retain forwarded storage even without local lexical accesses;
// they must come from these actual child occurrences, not arbitrary slots.
[[nodiscard]] std::variant<EvaluationPlan, EvaluationError> BuildEvaluationPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {},
    IncomingCells incoming = IncomingCells::kReject,
    std::span<const FunctionBinding> functions = {},
    std::span<const ModuleEvaluation> modules = {}, std::span<const CellDemand> demands = {},
    std::span<const ClassInitializerCall> initializers = {},
    analysis::ClosureWrites closure_writes = analysis::ClosureWrites::kUnknown,
    std::span<const ClassDefinitionSite> definitions = {},
    std::span<const analysis::OpId> receiver_guards = {});
// Iteratively expands the proposed forest in the declared input order, checking
// exact ordered occurrences and availability. Cross-phase expressions require
// contiguous effect intervals and equal catch snapshots at their final roots.
[[nodiscard]] std::variant<std::uint64_t, EvaluationError> VerifyEvaluationPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget = {});
// Same expression planner with phase availability and staged exceptional phi
// writes. Cross-phase fusion requires its independent corridor certificate.
// Normal entry points still refuse these plans; phased consumers verify catch
// scope and exceptional placement separately.
// Explicit closure_writes retains the same complete-family write obligation as
// normal evaluation, including buffered allocations with no child functions.
[[nodiscard]] std::variant<EvaluationPlan, EvaluationError> BuildPhasedEvaluationPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {},
    IncomingCells incoming = IncomingCells::kReject,
    std::span<const FunctionBinding> functions = {},
    std::span<const ModuleEvaluation> modules = {}, std::span<const CellDemand> demands = {},
    std::span<const ClassInitializerCall> initializers = {},
    analysis::ClosureWrites closure_writes = analysis::ClosureWrites::kUnknown);
[[nodiscard]] std::variant<std::uint64_t, EvaluationError> VerifyPhasedEvaluationPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
