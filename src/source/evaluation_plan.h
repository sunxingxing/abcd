#include "member_update.h"
#ifndef ABCD_SOURCE_EVALUATION_PLAN_H_
#define ABCD_SOURCE_EVALUATION_PLAN_H_
#include "for_of_evaluation_plan.h"
#include "template_construction.h"
#include "joint_selection_use.h"
#include "literal_field_types.h"

#include <span>
#include <algorithm>
#include <variant>
#include "abcd/source/source_tree.h"
#include "phi_copies.h"
#include "for_in_events.h"
#include "concurrent_completion.h"
#include "abcd/source/evaluation_failure.h"
#include "exception_copies.h"
#include "exception_copy_emission.h"
#include "literal_plan.h"
#include "cell_evaluation.h"
#include "module_guards.h"
#include "hole_protocol.h"
#include "class_initializers.h"
#include "class_self_order.h"
#include "../analysis/guarded_reads.h"
#include "abcd/source/call_reference.h"

namespace abcd::source {
enum class BranchPolarity : std::uint8_t { kTaken, kFallthrough };
// kPhiInput owns a sole executable incoming-copy RHS at its original edge.
// Original availability, effects and exceptional staging prove placement;
// it is neither a value snapshot nor permission for arbitrary movement.
// kLogicalLeft owns one native logical LHS: its original boolean use and
// retained phi input are two observations of that single expression result.
// kLogicalValue owns a logical/conditional result at one join consumer; logical_values
// identifies the eliminated phi location. Its cross-region inputs need interval proof.
// kPredicateSnapshot is the historical name for an eager use-site assignment:
// one consumer saves the value, all remaining occurrences read its private slot.
// Consumer syntax does not decide admission; reference/repeated-input boundaries
// and the independently checked effect interval still own that permission.
// kArrayCursor is a non-observable cursor/length step discharged by the
// current ArrayInitializationPlan, never a general-purpose pure elision.
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
  // Object-literal key ordinal when creation is a buffered object allocation.
  std::uint32_t property = analysis::kNoLiteralProperty;
  [[nodiscard]] std::pair<analysis::OpId, std::uint32_t> Key() const { return {creation, property}; }
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
struct PhaseCallReference {
  analysis::OpId callee, call;
  bool operator==(const PhaseCallReference&) const = default;
};
struct PhaseSelectionArm {
  analysis::OpId parent;
  bool operator==(const PhaseSelectionArm&) const = default;
};
// A source-control candidate, never an eager EvaluationPlan placement.
// The original branch writes its retained phi once. There is no eager use
// owner and no permission to move the family into a later consumer. Control
// binding and the independent AST catch-scope check discharge original scope.
struct PhaseSelectionBinding {
  bool operator==(const PhaseSelectionBinding&) const = default;
};
// Proof domain, not movement permission. Preserve the predicate-only schedule
// if it closes every member Reference; selection enclosure is a bounded retry.
// Every selection verifier independently reconstructs the selected domain.
enum class PhasePrefixDomain : std::uint8_t { kPredicate, kSelection };
struct PhaseSelectionOperand {
  // Direct eager operands and member arguments share control ownership. A
  // member Reference additionally retains its original callee/receiver order.
  // Control children have a typed parent and no eager input UseId; they share
  // original phi inputs and never claim a fabricated intermediate value.
  std::variant<analysis::OpId, PhaseCallReference, PhaseSelectionArm, PhaseSelectionBinding> owner;
  analysis::OpId branch;
  analysis::ValueId value;
  std::optional<analysis::UseId> input;
  std::optional<analysis::OpId> transport;
  // Syntax still consumes input at Consumer(). Inline pure dependencies and
  // checked outer selections carry the original execution/exception scope.
  analysis::OpId execution;
  std::vector<analysis::OpId> prefix;
  std::vector<PhaseCallReference> ancestors;
  [[nodiscard]] analysis::OpId Consumer() const {
    if (const auto* reference = std::get_if<PhaseCallReference>(&owner)) return reference->call;
    if (const auto* arm = std::get_if<PhaseSelectionArm>(&owner)) return arm->parent;
    if (std::holds_alternative<PhaseSelectionBinding>(owner)) return branch;
    return std::get<analysis::OpId>(owner);
  }
  bool operator==(const PhaseSelectionOperand&) const = default;
};
enum class GuardedReadRole : std::uint8_t { kRead, kGuard };
struct GuardedReadPeer { analysis::OpId operation; GuardedReadRole role; };
// An unmet member reference composed over complete, already verified normal
// selections. Branches retain their original phi/edge identities; no joint
// predicate/keep-edge or new selection admission is implied.
struct LogicalReferenceRoute {
  analysis::OpId call;
  std::vector<analysis::OpId> branches;
};
struct EvaluationPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<OpEvaluation> operations;
  // Empty functions retain no member-update arenas or per-vector headers.
  std::shared_ptr<const MemberUpdatePlan> member_updates;
  std::shared_ptr<const TemplateConstructions> templates;
  // Ordinary phi copies execute at incoming boundaries. Selected logical_values
  // consume both inputs in one expression instead; this table still proves
  // original values/schedules, not their eventual AST edge placement.
  std::optional<PhiCopies> phis;
  std::optional<ConcurrentCompletion> concurrent;
  std::optional<ForInPlan> for_in;
  std::optional<ForOfEvaluationPlan> for_of;
  std::optional<ForInEvents> for_in_events;
  std::optional<LiteralPlan> literals;
  std::optional<CellEvaluation> cells;
  std::vector<FunctionBinding> functions;
  std::vector<ModuleEvaluation> modules;
  // Native checked cell reads execute at the original guard, independently
  // rooted. The raw load is not an initialized/nonthrowing access proof.
  std::vector<analysis::GuardedRead> guarded_reads;
  std::vector<std::optional<GuardedReadPeer>> guarded_peers;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  bool object_rest = false;
  // Invocation count/tail obligation, not a native JS arguments binding.
  std::optional<analysis::ArgumentFrameFact> arguments;
  std::vector<std::optional<analysis::OpId>> assignment_values;
  std::vector<std::optional<analysis::UseId>> assignment_consumers;
  EvaluationFlow flow = EvaluationFlow::kNormal;
  SourceContract call_spread_contract = kSourceContract;
  std::optional<ExceptionCopies> exceptions;
  ExceptionStageDomain exception_stage_domain = ExceptionStageDomain::kOriginal;
  ExceptionCopyEmission exception_emission;
  // Flat, independently checked copy/phi identities in either flow. Exact
  // imports retain original transport anchors; optimization is optional.
  // Catch bindings and unresolved mutable phi
  // locations retain their original identity and first snapshot.
  std::vector<analysis::ValueId> aliases;
  std::optional<HoleProtocol> holes;
  std::vector<ClassInitializerCall> class_initializers;
  ClassProjection class_projection;
  std::vector<ClassDefinitionSite> class_definitions;
  std::vector<ClassPrototypeRead> class_prototype_reads;
  analysis::ClosureWrites closure_writes = analysis::ClosureWrites::kUnknown;
  // Conditional protocol obligation: the same derived owner, or actual capture
  // edges to its preserved receiver cells. The family factory discharges this
  // against unchanged input/work pairs after default recovery, never Language alone.
  std::vector<analysis::OpId> receiver_guards;
  std::optional<DerivedNativeProtocol> derived_protocol;
  // Phi values exclusively owned by native logical or conditional expressions, indexed by
  // ValueId. Each entry points to its original conditional branch operation.
  std::vector<std::optional<analysis::OpId>> logical_values;
  // Selected operands sorted by consumer and original UseId. Each phi owns
  // its original control interval; member operands additionally retain the
  // shared Reference and prefix. Ordinary inline edges differ.
  std::vector<PhaseSelectionOperand> phase_selections;
  PhasePrefixDomain phase_prefix_domain = PhasePrefixDomain::kPredicate;
  std::vector<std::optional<analysis::OpId>> phase_children;
  // Direct and ancestor callee references, indexed by original operation.
  // Entries are separately checked; they never license a .call substitution.
  std::vector<std::optional<analysis::UseId>> phase_references;
  // Exact owned occurrences of normal-flow closed pure expressions. Separate
  // from logical selection and phased event/catch certificates.
  std::vector<std::optional<analysis::UseId>> closed_expressions;
  std::vector<std::optional<analysis::UseId>> phase_transports;
  std::vector<std::optional<analysis::OpId>> phase_prefixes;
  // Ascending execution roots whose final-ownership trees were composed across
  // committed selections (phase_composition.h). Each is independently re-proved.
  std::vector<analysis::OpId> phase_compositions;
  std::vector<LogicalReferenceRoute> logical_reference_routes;
  std::vector<JointSelectionUse> joint_selection_uses;
  // Whole trees repairing unmet member references with checked capture events.
  // Separate from selection-only composition; every member is independently
  // re-proved before this certificate can replace corridor equations.
  std::vector<analysis::OpId> checked_compositions;
  std::vector<GuardedCallFailure> checked_failures; // Sorted by call; diagnostics only.
  // Original phi inputs certified by a private literal arm view. Independently
  // rederived before exempting these allocations from the phased root pin.
  std::vector<std::optional<analysis::UseId>> literal_arm_inputs;
  // Buffered allocations own a chain of staged definitions across source operands.
  // Indexed by allocation, pointing to the last definition. Normal selected
  // objects and phased operand composition share the native forest/placement.
  // Original snapshots, external leaves, effects and catches remain checked.
  std::vector<std::optional<analysis::OpId>> logical_objects;
  LiteralFields literal_fields;
  std::vector<NaNProjection> integer_nans;
  std::optional<ClassSelfOrder> self_order;
};
inline analysis::ValueId ResolveEvaluationValue(const EvaluationPlan& plan, analysis::ValueId value) {
  return plan.aliases.empty() ? value : plan.aliases[value.value];
}
inline std::optional<analysis::OpId> GuardedPeer(const EvaluationPlan& plan, analysis::OpId id) {
  if (id.value >= plan.guarded_peers.size() || !plan.guarded_peers[id.value]) return {};
  return plan.guarded_peers[id.value]->operation;
}
inline bool GuardedInput(const EvaluationPlan& plan, analysis::OpId id) {
  return id.value < plan.guarded_peers.size() && plan.guarded_peers[id.value] &&
      plan.guarded_peers[id.value]->role == GuardedReadRole::kRead;
}
struct ArraySourceInput {
  SourceContract contract = kSourceContract;
  ExpressionConstants constants;
  std::span<const std::u16string> names;
  std::optional<ArrayInitializationError>* failure = nullptr;
};
inline bool TemplateOwned(const EvaluationPlan& p, analysis::OpId op) { return p.templates && p.templates->owners[op.value].has_value(); }
inline bool TemplateHidden(const EvaluationPlan& p, analysis::UseId use) { return p.templates && p.templates->hidden[use.value]; }
inline const TemplateConstruction* TemplateSite(const EvaluationPlan& p, analysis::OpId op) {
  if(!p.templates)return nullptr;
  auto i=std::lower_bound(p.templates->sites.begin(),p.templates->sites.end(),op,[](const auto& site,auto id){return site.operation<id;});
  return i!=p.templates->sites.end()&&i->operation==op?&*i:nullptr;
}
inline const ArrayInitializationPlan* ArrayInitializations(const EvaluationPlan& plan) {
  return plan.literals && plan.literals->arrays ? &*plan.literals->arrays : nullptr;
}
inline const ArrayInitialization* ArrayLiteralFor(const EvaluationPlan& plan, analysis::OpId id) {
  const auto* arrays = ArrayInitializations(plan); if (!arrays) return nullptr;
  const auto found = std::lower_bound(arrays->arrays.begin(), arrays->arrays.end(), id,
      [](const auto& array, auto target) { return array.allocation < target; });
  return found != arrays->arrays.end() && found->allocation == id ? &*found : nullptr;
}
inline bool IsArrayInitialization(const EvaluationPlan& plan, analysis::OpId id) {
  const auto* arrays = ArrayInitializations(plan);
  return arrays && id.value < arrays->owners.size() && arrays->owners[id.value].has_value();
}
inline bool IsArrayOperand(const EvaluationPlan& plan, analysis::OpId id) {
  const auto* arrays = ArrayInitializations(plan);
  return arrays && id.value < arrays->nested.size() && arrays->nested[id.value].has_value();
}
inline std::optional<analysis::UseId> NestedLiteralUse(const EvaluationPlan& plan, analysis::OpId id) {
  if (const auto* arrays = ArrayInitializations(plan); arrays && id.value < arrays->nested.size() && arrays->nested[id.value])
    return arrays->nested[id.value];
  return plan.literals && id.value < plan.literals->nested.size() ? plan.literals->nested[id.value] : std::nullopt;
}
inline bool IsArrayCursor(const EvaluationPlan& plan, analysis::OpId id, const analysis::ValueProgram& graph) {
  return IsArrayInitialization(plan, id) &&
      !std::holds_alternative<analysis::ValueArraySpread>(graph.ops[id.value].operation) &&
      !std::holds_alternative<analysis::ValueComputedOwnWrite>(graph.ops[id.value].operation) &&
      !std::holds_alternative<analysis::ValueIndexedWrite>(graph.ops[id.value].operation);
}
// After fresh evaluation verification: Hole transport closed over copies and
// absent heritage has positions, but no JS value/binding. Phased comparisons,
// rethrows and phi storage retain their existing source-token representation.
inline bool IsAbsentHeritageTransport(const analysis::ValueProgram& graph,
                                      const EvaluationPlan& plan,
                                      const analysis::ValueOp& op) {
  return plan.holes &&
         (plan.flow == EvaluationFlow::kNormal ||
          (!plan.holes->phi_transport && plan.holes->comparisons.empty() &&
           plan.holes->rethrows.empty())) &&
         op.result && op.normal == analysis::LanguageEffect::kNone &&
         op.before_throw == analysis::LanguageEffect::kNone &&
         op.exceptional == analysis::ExceptionalCompletion::kImpossible &&
         graph.values[op.result->value].domain ==
             analysis::ValueKinds::Hole() &&
         (std::holds_alternative<analysis::ValueHole>(op.operation) ||
          std::holds_alternative<analysis::ValueCopy>(op.operation));
}
inline bool NeedsHoleToken(const EvaluationPlan& plan) {
  if (!plan.holes || plan.flow != EvaluationFlow::kPhased) return false;
  if (plan.holes->phi_transport) return true;
  // The implicit native close owns these sentinel tests/rethrows. A VM Hole
  // token is needed only by surviving source operations, never for that state.
  for (const auto& comparison : plan.holes->comparisons)
    if (!plan.for_of || plan.operations[comparison.operation.value].kind !=
                            EvaluationKind::kForOfEvent)
      return true;
  for (const auto& rethrow : plan.holes->rethrows)
    if (!plan.for_of || plan.operations[rethrow.operation.value].kind !=
                            EvaluationKind::kForOfEvent)
      return true;
  return false;
}
inline analysis::ValueOperand ResolveEvaluationOperand(
    const EvaluationPlan& plan, const analysis::ValueOperand& operand) {
  if (const auto* value = std::get_if<analysis::ValueId>(&operand))
    return ResolveEvaluationValue(plan, *value);
  return operand;
}

enum class ReferenceBoundary : std::uint8_t { kNone, kBeforeStoredValue };
// ConditionalArgument reads a declared slot only when ordinal < actual count.
// A later AST must bind that count and the extra tail to this invocation; this
// scheduling mode never authorizes an eager call-argument array or rest rewrite.
// ClassHeritage preserves the class-creation protocol occurrence and evaluates
// language heritage in order; definite Hole means absent extends. Scheduling
// does not establish constructor/member bodies, captures or inheritance syntax.
// ConstructorGuard retains the VM check as an ordered event and snapshots its
// input. It does not authorize guard elision or reading uninitialized JS this.
// kPatternRecord: the private array-pattern record, read only by step/close.
enum class InputEvaluation : std::uint8_t { kEager, kConditionalArgument, kClassHeritage, kReceiverGuard, kInvocationConstructor, kPatternRecord };
// Source lowering capability, independent of opcode layout and SSA ownership.
// Snapshot inputs can expand to repeated reads; suspension and invocation
// protocols need their own control/ABI owner before expression composition.
enum class SourceEvaluation : std::uint8_t { kDirect, kPure, kSnapshotInputs, kSuspending, kInvocation, kUnsupported };
// Normal control observes existing operands and chooses successors. It has no
// language effect of its own; purity and all-path execution still need proof.
enum class ControlEvaluation : std::uint8_t { kNone, kNormal };
// Native invocation storage, separate from a derived VM's undefined entry.
// Initialization itself remains an owned, potentially throwing super event.
enum class ReceiverValue : std::uint8_t { kNone, kCurrent, kInitialized };
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
  // The current invocation constructor is a protocol identity, never a JS
  // variable. Derived lowering must separately prove its actual origin.
  bool invocation_constructor = false;
  SourceEvaluation source = SourceEvaluation::kDirect;
  ControlEvaluation control = ControlEvaluation::kNone;
  ReceiverValue receiver = ReceiverValue::kNone;
  // Source [[Set]] expression yields the stored value only after successful write.
  std::optional<analysis::OperandRole> assignment_result;
  [[nodiscard]] bool NeedsArgumentFrame() const {
    return inputs == InputEvaluation::kConditionalArgument || inputs == InputEvaluation::kInvocationConstructor;
  }
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
// A language expression with no dependencies, effects, implicit context or
// abrupt completion. Fresh availability/ownership still governs each use.
[[nodiscard]] bool IsClosedPureValue(const analysis::ValueProgram&, analysis::OpId);
// Constant-time semantic checks. Conditional slot uses remain protocol reads:
// their producers execute at the original point, never inside a count predicate.
[[nodiscard]] std::optional<EvaluationFailure> CheckArgumentSnapshot(
    const analysis::ValueProgram& graph, const analysis::ValueOp& operation);
[[nodiscard]] std::optional<EvaluationFailure> CheckEvaluationUse(
    const analysis::ValueProgram& graph, const analysis::OperandUse& use,
    const EvaluationShape& shape, std::uint32_t ordinal);
[[nodiscard]] bool BooleanEvaluation(const analysis::ValueProgram& graph,
                                     const analysis::ValueOperand& operand);
// Identity conversion requires an existing domain proof, never a source name
// or apparent loop-counter shape. Arbitrary Language includes BigInt/coercion.
[[nodiscard]] bool LegacyNegationEvaluation(const analysis::ValueProgram& graph,
                                          const analysis::ValueOperand& operand,
    analysis::ArithmeticSemantics arithmetic = analysis::ArithmeticSemantics::kPinnedRuntime);
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
[[nodiscard]] std::variant<EvaluationPlan, EvaluationFailure>
BuildEvaluationPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {},
    IncomingCells incoming = IncomingCells::kReject,
    std::span<const FunctionBinding> functions = {},
    std::span<const ModuleEvaluation> modules = {},
    std::span<const CellDemand> demands = {},
    std::span<const ClassInitializerCall> initializers = {},
    analysis::ClosureWrites closure_writes = analysis::ClosureWrites::kUnknown,
    std::span<const ClassDefinitionSite> definitions = {},
    std::span<const analysis::OpId> receiver_guards = {},
    std::span<const IncomingCell> entry_demands = {},
    const ClassSelfOrder* self_order = nullptr,
    std::span<const ClassPrototypeRead> prototype_reads = {},
    ArraySourceInput arrays = {},
    const DerivedNativeProtocol* derived = nullptr);
// Iteratively expands the proposed forest in the declared input order, checking
// exact ordered occurrences and availability. Cross-phase expressions require
// contiguous effect intervals and equal catch snapshots at their final roots.
[[nodiscard]] std::variant<std::uint64_t, EvaluationFailure> VerifyEvaluationPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget = {});
// Same expression planner with phase availability and staged exceptional phi
// writes. Cross-phase fusion requires its independent corridor certificate.
// Normal entry points still refuse these plans; phased consumers verify catch
// scope and exceptional placement separately.
// Explicit closure_writes retains the same complete-family write obligation as
// normal evaluation, including buffered allocations with no child functions.
[[nodiscard]] std::variant<EvaluationPlan, EvaluationFailure> BuildPhasedEvaluationPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {},
    IncomingCells incoming = IncomingCells::kReject,
    std::span<const FunctionBinding> functions = {},
    std::span<const ModuleEvaluation> modules = {}, std::span<const CellDemand> demands = {},
    std::span<const ClassInitializerCall> initializers = {},
    analysis::ClosureWrites closure_writes = analysis::ClosureWrites::kUnknown, ArraySourceInput arrays = {},
    std::span<const analysis::GuardedRead> guarded = {}, const ForOfEvaluationInput* for_of = nullptr,
    const ClassSelfOrder* self_order = nullptr);
[[nodiscard]] std::variant<std::uint64_t, EvaluationFailure> VerifyPhasedEvaluationPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget = {}, const ForOfEvaluationInput* for_of = nullptr);
}  // namespace abcd::source
#endif
