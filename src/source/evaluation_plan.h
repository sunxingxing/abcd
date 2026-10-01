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
// kPhiInput owns a sole incoming-copy RHS at the same block exit. It is
// neither a value snapshot nor permission to cross a branch/exception edge.
// kLogicalLeft owns one native logical LHS: its original boolean use and
// retained phi input are two observations of that single expression result.
enum class EvaluationKind : std::uint8_t {
  kRemoved, kRoot, kInline, kInitializer, kEnvironment, kModuleGuard, kElidedPure, kAlias, kNestedLiteral, kClassProjection, kConstructorGuard, kCellGuard, kPhiInput, kLogicalLeft
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
// The raw load and Hole check jointly become one throwing live-binding read.
struct ModuleEvaluation {
  GuardedModuleRead pair;
  analysis::ValueModuleRead read;
  ModuleSymbol target;
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
  // Conditional same-owner protocol obligation. The source factory must
  // reprove these guards against the unchanged input after default recovery.
  std::vector<analysis::OpId> constructor_guards;
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
enum class InputEvaluation : std::uint8_t { kEager, kConditionalArgument, kClassHeritage, kConstructorGuard };
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
    return inputs == InputEvaluation::kConditionalArgument || inputs == InputEvaluation::kConstructorGuard ||
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
    std::span<const analysis::OpId> constructor_guards = {});
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
[[nodiscard]] std::variant<EvaluationPlan, EvaluationError> BuildPhasedEvaluationPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {},
    IncomingCells incoming = IncomingCells::kReject,
    std::span<const FunctionBinding> functions = {},
    std::span<const ModuleEvaluation> modules = {}, std::span<const CellDemand> demands = {},
    std::span<const ClassInitializerCall> initializers = {});
[[nodiscard]] std::variant<std::uint64_t, EvaluationError> VerifyPhasedEvaluationPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
