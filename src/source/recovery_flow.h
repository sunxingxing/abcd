#ifndef ABCD_SOURCE_RECOVERY_FLOW_H_
#define ABCD_SOURCE_RECOVERY_FLOW_H_
#include "expression_transport.h"
#include "tree_structure.h"
#include "expression_observation.h"
#include "primitive_value.h"
#include "original_flow.h"
#include "flow_scopes.h"
#include "cell_access.h"
#include "truth_domains.h"
#include <type_traits>
namespace abcd::source {
struct FlowPoint { std::uint32_t value = 0; auto operator<=>(const FlowPoint&) const = default; };
// Checked raw result and its current source evaluation interval. The normal
// continuation excludes the throwing route and never certifies a later store.
// Missing entries are correspondence gaps, not permission to reuse raw effects.
struct FlowOriginalResult { ExprId expression; FlowPoint entry,normal; };
// The completed input may lead to a shared source decision point. Keep its
// own entry/normal interval: the successor alone cannot establish this
// occurrence's execution. This fact does not identify a selected value.
struct FlowOriginalControl {ExprId expression;FlowPoint entry,normal;OriginalControlKind kind;std::optional<PureTruthTest> test;};
struct FlowControlDomains {TruthDomains evaluated,true_values,false_values;};
// A folded pure test has Before/Commit on its qualified input domain, not an
// unconditional physical point. Resolve the operation through the current
// control occurrence; never discard its path by using input.normal alone.
struct FlowControlPhase {analysis::OpId operation;};
// Interior states of a verified native action composition have no individual
// physical point. Keeping them typed prevents pre-guard values or effects from
// borrowing the native action's successful completion.
struct FlowRegionPhase {OriginalActionRegionId region;};
// Hoisted invocation creation has a value relationship, not a physical raw
// control point. Its source initialization remains outside body handlers.
struct FlowInvocationPhase {ParameterId parameter;};
// An erased initialized guard qualifies a captured value. Its VM location is
// not the load's physical action or a later expression's completion.
struct FlowGuardPhase {analysis::OpId operation;};
struct FlowForwardedTryPhase {analysis::TryId region; analysis::WorkBlockId block;};
struct FlowForwardedPhase {StmtId statement; analysis::WorkBlockId block;};
enum class FlowIterationPhaseKind : std::uint8_t { kPrivate, kBindingCommit };
struct FlowIterationPhase { IteratorProtocolId protocol; analysis::WorkBlockId original; FlowIterationPhaseKind kind=FlowIterationPhaseKind::kPrivate; };
using FlowOriginalPhase=std::variant<FlowPoint,FlowControlPhase,FlowRegionPhase,FlowInvocationPhase,FlowGuardPhase,FlowForwardedPhase,FlowForwardedTryPhase,FlowIterationPhase>;
struct FlowIterationEvent {ForOfEventKind kind;FlowPoint before,commit,thrown;};
// A native close is effectful and has completion-dependent exception priority.
// It is never a transparent handler forwarding record.
enum class FlowCompletion : std::uint8_t;
struct FlowPendingPayload { FlowPoint entry; bool operator==(const FlowPendingPayload&)const=default; };
struct FlowCloseErrorPayload { FlowPoint operation; bool operator==(const FlowCloseErrorPayload&)const=default; };
using FlowClosePayload=std::variant<FlowPendingPayload,FlowCloseErrorPayload>;
struct FlowCloseOutcome {
  analysis::CloseOutcomeKind outcome;
  analysis::PendingCompletionKind completion;
  FlowClosePayload payload;
  FlowPoint target;
};
inline constexpr std::size_t kCloseOutcomeCount=static_cast<std::size_t>(analysis::CloseOutcomeKind::kPrimitiveResult);
struct FlowIterationClose {
  StmtId loop;
  analysis::PendingCompletionKind pending;
  FlowPoint entry,read,missing,call,check,success,getter_error,call_error,primitive;
  FlowPoint target,thrown;
  FlowCompletion completion;
  std::array<FlowCloseOutcome,kCloseOutcomeCount> outcomes;
};
struct FlowIteration {
  StmtId statement;
  FlowPoint rhs,done,write,commit,body,exit,thrown;
  std::optional<FlowPoint> finalizer;
  std::array<FlowIterationEvent,kForOfEventCount> events;
  std::uint32_t throw_close;
};
// Native ports are separate even when a VM result has no source expression.
struct FlowEnumeration {
  StmtId statement;
  FlowPoint rhs, acquire_before, acquire_commit, next_before, next_commit;
  FlowPoint done, key_write, key_commit, body, exit;
  FlowPoint thrown;
  std::optional<FlowPoint> finalizer;
};
// Commits are path qualified; only initialized commits define the formal in
// the residual. The raw argument value has no fabricated source ExprId.
struct FlowParameterDefault {
  ParameterId parameter;
  FlowPoint argument, decision, provided, missing, initialized;
  ExprId initializer;
  FlowPoint caller_exception;
};
struct FlowDefinitionId { std::uint32_t value = 0; auto operator<=>(const FlowDefinitionId&) const = default; };
struct FlowInputId { std::uint32_t value = 0; auto operator<=>(const FlowInputId&) const = default; };
struct DiscardedId { std::uint32_t value = 0; auto operator<=>(const DiscardedId&) const = default; };
// An ignored expression result, with effects composed by the semantic adapter.
// Membership does not authorize changing control or binding lifetime.
struct DiscardedEvaluation {
  StmtId statement;
  ExpressionEvaluation effect = ExpressionEvaluation::kInert;
  // An initializer's result feeds this binding rather than an external value.
  // Value demand does not by itself authorize removing its lexical completion.
  std::optional<SymbolId> initializer;
  std::optional<ExprId> evaluation;
};
enum class FlowCompletion : std::uint8_t { kNormal, kThrow, kReturn, kJump, kTaken, kNotTaken, kResumeNext, kResumeThrow, kResumeReturn, kConservativeCleanup };
enum class FlowDefinitionKind : std::uint8_t { kParameter, kRestParameter, kUndefined, kInitializer, kAssignment, kUpdate, kCatch, kUndefinedStore, kFunctionSelf, kIterationCopy };
enum class FlowEventKind : std::uint8_t { kControl, kTruthBranch, kNullishBranch, kRead, kEvaluation, kAggregateDefinition, kEnterScope, kLeaveScope, kDefinition, kSuspend, kPropertyReference, kEnterLoopEnvironment, kCopyInput, kEnterIterationEnvironment, kLeaveLoopEnvironment, kEnumerationAcquire, kEnumerationNext, kEnumerationBind, kConcurrentNotification };
enum class FlowValueUse : std::uint8_t { kUnmodeled, kDiscarded, kObserved };
enum class FlowWriteEffect : std::uint8_t { kObservable, kInert };
enum class FlowBindingExposure : std::uint8_t { kObservable, kPrivate };
struct SourceRegionId {std::uint32_t value=0;auto operator<=>(const SourceRegionId&)const=default;};
struct SourcePosition {std::size_t value=0;auto operator<=>(const SourcePosition&)const=default;};
struct SourceRegion {SourcePosition first,last;};
struct OriginalBindingScope {std::optional<SourceRegionId> region;};
struct ProjectedBindingScope {SourceRegionId region;};
struct UnmodeledBindingScope {};
// Explicit external environment: self/module/capture/parameter activation
// encloses the body. Absence of a local region never establishes this fact.
struct EnclosingBindingScope {};
// Lexical containment only: dynamic cell identity, initialization completion
// and source spelling remain separate obligations. Missing projection facts
// cannot authorize access to a narrowed declaration.
using SourceBindingScope=std::variant<OriginalBindingScope,ProjectedBindingScope,UnmodeledBindingScope,EnclosingBindingScope>;
// A canonical inner scope shadows this enclosing binding's source spelling.
// Missing region is an explicit precision gap; this fact never merges cells.
struct SourceBindingShadow {
  SymbolId binding;
  std::optional<SourceRegionId> region;
  // Canonical shadowing identity enables current retention projection. Absent
  // on legacy facts: preserve their conservative region, never guess identity.
  std::optional<SymbolId> shadowing;
};
enum class SourceNameResolution : std::uint8_t {kResolved,kShadowed,kUnavailable,kUnmodeled,kAmbiguous};
struct SourceNameRange {SourceSpellingId spelling;SourceRegion region;SymbolId binding;};
struct SourceNameIndex {
  std::vector<SourceNameRange> ranges;
  std::vector<SourceNameResolution> precision;
  // Nearest retained spelling at each binding entry, including elided entries.
  // Coincident local regions share a peer identity for declaration conflicts.
  std::vector<std::optional<SymbolId>> enclosing;
  std::vector<std::optional<SymbolId>> scope_peer;
};
struct SourceNameQuery {std::variant<SymbolId,NameId> name;std::optional<StmtId> owner;};
struct UnmodeledSourceAllocation {};
// Transitional native-loop contract: existing placement/confinement/capture
// proofs preserve completed scalar values, but do not certify dynamic units.
// Keep this contract explicit until copied iteration environments replace it.
struct LegacyLoopAllocation {};
// Native header entry allocation, distinct from the copied iteration units.
struct NativeLoopAllocation {StmtId loop,entry;};
using SourceAllocation=std::variant<UnmodeledSourceAllocation,ScopeId,LegacyLoopAllocation>;
struct SourceScopeProjection {
  std::span<const SourceRegion> regions;
  std::span<const std::optional<SourcePosition>> occurrences;
  std::span<const SourceBindingScope> bindings;
  std::span<const SourceBindingShadow> shadows;
  std::span<const std::uint8_t> unique_spellings;
  std::span<const SourceSpellingId> spellings;
  std::span<const std::optional<SourceSpellingId>> names;
  // Region-indexed current allocation provenance, published by the shared
  // lexical census. Absence denotes an unmodeled projected environment;
  // region positions and original SymbolId scopes cannot infer its unit.
  std::span<const SourceAllocation> allocations;
  // Actual native BoundNames, indexed by binding, not lexical-region peers.
  std::span<const std::optional<NativeLoopAllocation>> loop_allocations;
  // Actual native header occurrences, including descendants of declarators.
  std::span<const std::optional<StmtId>> loop_owners;
};
// A lexical scope activation allocates a new unit on every visit. ScopeId
// names the allocation site, never the dynamic cell. Completion-only inputs
// keep synthetic graph contracts; enclosing units have independent stability
// and exposure proofs. Projected lexical scopes remain a separate obligation.
struct CompletionOnlyLifetime {auto operator<=>(const CompletionOnlyLifetime&)const=default;};
struct EnclosingBindingLifetime {auto operator<=>(const EnclosingBindingLifetime&)const=default;};
struct UnmodeledBindingLifetime {auto operator<=>(const UnmodeledBindingLifetime&)const=default;};
struct ScopeBindingLifetime { ScopeId scope; FlowPoint enter,leave; auto operator<=>(const ScopeBindingLifetime&)const=default;};
struct LoopBindingLifetime {StmtId loop;auto operator<=>(const LoopBindingLifetime&)const=default;};
using FlowBindingLifetime=std::variant<CompletionOnlyLifetime,EnclosingBindingLifetime,ScopeBindingLifetime,UnmodeledBindingLifetime,LoopBindingLifetime>;
struct FlowOriginalUnit {
  std::optional<ScopeId> original; // Absence is the unchanged enclosing unit.
  FlowBindingLifetime current;
  std::optional<FlowPoint> allocation;
  std::optional<OriginalCellActivation> original_activation;
};
struct FlowBinding {
  BindingKind kind;
  FlowBindingExposure exposure = FlowBindingExposure::kObservable;
  std::optional<StmtId> declaration;
  std::optional<FlowDefinitionKind> initialization;
  SourceBindingScope scope=OriginalBindingScope{};
  bool unique_spelling=true;
  // Semantic adapter proof: body-entry identity cannot be changed by mapped
  // arguments, unknown captures or unmodeled signature evaluation. Immutable
  // values separately reject body writes; current-copy equality kills at each
  // successful write. Neither analysis grounds local activation identities.
  bool stable_entry=false;
  // Absence belongs to legacy graph clients. Current/trial source adapters
  // always publish this fact; it neither permits writes nor grants invariance.
  std::optional<CellVisibility> cell_access;
  [[nodiscard]] bool MayBeChangedByUserCode() const {
    if(cell_access)return *cell_access!=CellVisibility::kClosed;
    return exposure!=FlowBindingExposure::kPrivate&&!stable_entry;
  }
  FlowBindingLifetime lifetime=CompletionOnlyLifetime{};
  // First allocation must precede declaration completion. This is an order
  // certificate, never equality of cells across copied loop environments.
  std::optional<FlowPoint> allocation;
};
enum class FlowReferenceKind : std::uint8_t { kRead, kWrite };
using FlowReferenceSite=std::variant<ExprId,StmtId>;
struct FlowExceptionContext {
  FlowPoint thrown;
  std::optional<FlowPoint> finalizer;
  auto operator<=>(const FlowExceptionContext&) const = default;
};
// Native action boundaries follow operand evaluation. Reference preparation
// precedes access; an update's conversion follows its captured read. Normal
// completion includes successful source writeback, never the throwing route.
struct FlowAction {FlowPoint before,normal;FlowExceptionContext exceptions;};
// Native signature evaluation belongs to the invocation, outside body handlers.
// Parameter ordinal identifies the source value; storage and later reads are
// separate. Creation succeeds before the initialization definition commits.
struct FlowInvocationValue {ParameterId parameter;FlowPoint creation;FlowDefinitionId definition;};
// Static data literals allocate before their ordered value evaluations. Each
// own definition follows its value's normal completion; no inherited setter,
// proxy trap or key conversion belongs to these fresh ordinary aggregates.
struct FlowAggregate {
  ExprId expression;
  FlowPoint allocation,allocated,normal;
  PropertyRange properties;
  FlowExceptionContext exceptions;
};
// A verified private array printed as positional arguments has no source
// allocation/result or property writes. Its ordered value evaluations remain.
struct FlowArgumentCarrier {ExprId expression;FlowPoint entry,normal;PropertyRange properties;};
// Native argument-snapshot lowering guards each slot by invocation arity.
// Both edges remain possible until the common model knows that arity.
struct FlowSnapshotSlot {ExprId snapshot;std::uint32_t ordinal;FlowPoint branch,value,normal;};
// One current correspondence for phases and exception routes. Before/context
// come from the action; normal denotes raw Commit, following the separately
// checked result projection when present or the physical write otherwise.
// A throwing endpoint has no normal completion; this is not a result domain.
struct FlowOriginalAction {FlowPoint before;std::optional<FlowPoint> normal;FlowExceptionContext exceptions;};
// Statement actions share the same Before/Commit/exception contract as
// expression actions. Payload evaluation precedes return/throw endpoints;
// a normal statement's next point is its own successful completion.
struct FlowStatementAction {OriginalStatementKind kind;FlowOriginalAction action;};
struct FlowOriginalException {FlowPoint before,thrown;};
struct FlowForwardedException {FlowPoint before,thrown; ForInExceptionExitId exit; analysis::WorkBlockId handler; analysis::ValueId payload;};
// A contract-excluded failure has no physical source throw point. Keeping it
// distinct from both a mapped route and missing evidence prevents borrowing
// a declaration/scope entry as the VM allocation action.
struct FlowExcludedResourceFailure {};
// This exclusion is conditional on the current input's provenance. The joint
// solver carries both its value and completion dependencies into the handler;
// changing initialization, occurrence or cell identity withdraws raw catch caps.
struct FlowExcludedGuardFailure {ExprId value;FlowPoint input,target;};
enum class FlowIterationFailureKind : std::uint8_t { kStep, kCloseRead, kCloseCall, kCloseCheck, kPrivate };
struct FlowIterationException {IteratorProtocolId protocol;std::uint32_t original_exit;std::optional<FlowPoint> before;FlowPoint thrown;FlowIterationFailureKind kind=FlowIterationFailureKind::kStep;};
struct FlowForwardedTryDispatch {std::uint32_t exit;analysis::TryId region;};
using FlowOriginalFailure=std::variant<FlowOriginalException,FlowForwardedException,FlowForwardedTryDispatch,FlowExcludedResourceFailure,FlowExcludedGuardFailure,FlowIterationException>;
enum class FlowOriginalRouteCoverage : std::uint8_t {kUnmodeled,kComplete};
struct FlowReference {
  FlowPoint point;
  SymbolId binding;
  FlowReferenceKind kind;
  // Statement sites denote lexical initialization or projected writes. Expression
  // sites retain source identity even when forwarded to a new flow position.
  FlowReferenceSite site;
  // Retain the handler/cleanup route even if the reference itself is proved
  // nonthrowing. Moving a producer here must preserve its exceptional route.
  std::optional<FlowExceptionContext> exceptions;
  // Current occurrence owner, transported through replacement/forwarding.
  // The source expression identity alone cannot locate a moved evaluation.
  std::optional<StmtId> owner;
  // Original operand occurrences, distinct from current projected binding.
  std::optional<OriginalInputRange> original;
};
struct FlowUnitCapture {FlowPoint point;SymbolId binding;};
struct FlowReferenceId {std::uint32_t value;};
struct FlowPropertyId {std::uint32_t value=0;auto operator<=>(const FlowPropertyId&)const=default;};
enum class FlowPropertyPhase : std::uint8_t {kAcquisition,kReceiver,kKey,kAccess};
struct FlowPropertySite {FlowPropertyId reference;FlowPropertyPhase phase;auto operator<=>(const FlowPropertySite&)const=default;};
// ECMA-262 2025 13.3.3/6.2.5: acquisition retains base/unconverted key.
// GetValue/PutValue check the receiver, convert the key, then Get/Set.
// Assignment evaluates RHS between acquisition and PutValue. Never move the
// 2024 early conversion here; see reference/source-property-references.md.
struct FlowPropertyReference {
  FlowPoint acquisition,receiver,key_conversion,access,normal;
  FlowReferenceKind kind;
  ExprId site,base;
  MemberKey key;
  FlowExceptionContext exceptions;
  std::optional<StmtId> owner;
  // Checked original operands, not current binding/cell identity proofs.
  std::optional<OriginalInputRange> original_base,original_key;
  analysis::PropertyLookup lookup = analysis::PropertyLookup::kObject;
};
// Free lexical names are observations, not bindings with local definitions.
// Their original expression and current owner are distinct after forwarding.
struct FlowNameReference {
  FlowPoint point;
  NameId name;
  ExprId site;
  std::optional<StmtId> owner;
};
struct LoopSyntax;
struct BindingInitializer;
struct DeclarationValue;
struct DeclarationCompletion;
// A captured value belongs to this read occurrence, not the current binding
// at its eventual commit. Native environment copies produce this input once;
// demand/value consumers must preserve the old-value dependency across resets
// and intervening conversions. Definition kind owns any numeric transformation;
// merely having a captured input is not a copy/equality certificate.
struct FlowCapturedValue {FlowPoint point;SymbolId binding;auto operator<=>(const FlowCapturedValue&)const=default;};
// Implicit action operands use the value at their read, never the binding at
// conversion or commit. Current/trial builders publish the same dependency;
// the joint solve owns its kinds, independently of the action's result kinds.
struct FlowCapturedInput {
  FlowPoint action;
  FlowCapturedValue value;
  analysis::ValueKinds kinds=analysis::ValueKinds::Language();
};
struct FlowEnumerationDefinition { StmtId statement; std::optional<ForInKeyCommit> original; };
struct FlowDefinition {
  FlowPoint point;
  SymbolId binding;
  FlowDefinitionKind kind;
  std::optional<SymbolId> copy;
  std::optional<ExprId> value;
  std::optional<StmtId> declaration;
  std::optional<ExprId> write;
  FlowWriteEffect effect = FlowWriteEffect::kObservable;
  // Conditional provenance from the checked original write. The common value
  // dependency analysis decides whether its domain survived this projection.
  const OriginalSourceDefinition* original=nullptr;
  std::optional<FlowCapturedValue> input;
  // Catch activation is a source definition, distinct from its later staging
  // stores. Pending-value precision depends on current incoming completions.
  const OriginalCatchBinding* original_catch=nullptr;
  std::optional<FlowPoint> catch_entry;
  // Success-edge key commit, not the raw next result on exhaustion.
  std::optional<FlowEnumerationDefinition> enumeration;
  std::optional<StmtId> iteration;
  // The semantic adapter owns both expression and statement write positions.
  // Kind still distinguishes lexical completion from assignment/slot writes;
  // a site alone never authorizes deleting initialization or binding lifetime.
  [[nodiscard]] std::optional<FlowReferenceSite> WriteSite() const {
    if(write)return FlowReferenceSite{*write};
    if(declaration)return FlowReferenceSite{*declaration};
    return std::nullopt;
  }
};
// Language statement ownership is adapted once by the flow builder. These
// conservative completion/progress obligations are independent of layout and
// expression effects; an empty loop still may not terminate.
enum class FlowStatementCompletion : std::uint8_t {kNormal,kAbrupt,kCleanup,kJump};
enum class FlowStatementProgress : std::uint8_t {kFinite,kMayRepeat};
// Required grammar positions may have no observable computation. Keep the
// container itself without manufacturing effects that retain its ancestors.
enum class FlowStatementRetention : std::uint8_t {kOptional,kRequired};
struct FlowStatement {
  std::optional<StmtId> parent;
  FlowStatementCompletion completion=FlowStatementCompletion::kNormal;
  FlowStatementProgress progress=FlowStatementProgress::kFinite;
  // Actual continuation/repetition identities from the language adapter.
  // Missing evidence stays conservative; source layout is never a substitute.
  std::optional<FlowPoint> normal_exit,repeat_point;
  // Mandatory entry evaluation runs once at this statement, before its owned
  // control region. Its original scope/handler and expression identity remain.
  std::optional<ExprId> entry_value;
  FlowStatementRetention retention=FlowStatementRetention::kOptional;
};
// Conditional raw proofs are valid only for an unchanged execution cone.
// This is adapter provenance, not a claim that projected code is inequivalent
// or that raw effects equal source effects. Legacy graph clients lack it.
enum class FlowProvenance : std::uint8_t {kUnmodeled,kPreserved,kChanged};
enum class FlowReadCompletion : std::uint8_t {kUnproved,kInitialized};
struct FlowEvent {
  // Group narrow fields before the size_t links; retain all phase facts without
  // increasing padding in every admitted event. Construction uses the typed
  // kind/read/definition/expression/scope prefix, never physical field offsets.
  FlowEventKind kind = FlowEventKind::kControl;
  std::optional<SymbolId> read;
  std::optional<FlowDefinitionId> definition;
  std::optional<ExprId> expression;
  std::optional<ScopeId> scope;
  std::optional<DiscardedId> discarded;
  std::optional<StmtId> owner;
  std::optional<FlowPropertySite> property;
  std::optional<FlowInputId> input;
  std::optional<std::size_t> incoming, outgoing;
  std::optional<StmtId> loop_environment;
  // Explicit throw evaluates this value before publishing the exception.
  // Implicit exceptions/resume failures have no known language payload.
  std::optional<ExprId> thrown_value;
  // Exact current intrinsic phases from the sole language semantic table.
  // Missing facts retain the conservative Required fallback for graph clients.
  std::optional<SourceEffects> effects;
  // Intrinsic effect at this event, refined with binding completion. State
  // reads and successful writes remain observations even when this is inert.
  ExpressionEvaluation effect = ExpressionEvaluation::kInert;
  bool entry_evaluation=false;
  bool original_effect_valid=false;
  FlowProvenance provenance=FlowProvenance::kUnmodeled;
  // Current-projection must-initialization at this lexical GetValue only.
  // It says nothing about a subsequent conversion, call or successful store.
  FlowReadCompletion read_completion=FlowReadCompletion::kUnproved;
  [[nodiscard]] bool ResetsScopeUnit() const {
    return kind==FlowEventKind::kEnterScope||kind==FlowEventKind::kLeaveScope;
  }
  [[nodiscard]] bool ResetsLoopUnit() const {
    return kind==FlowEventKind::kEnterLoopEnvironment||kind==FlowEventKind::kEnterIterationEnvironment||
        kind==FlowEventKind::kLeaveLoopEnvironment;
  }
  [[nodiscard]] bool MayInvokeUserCode(FlowCompletion completion) const {
    if(completion==FlowCompletion::kConservativeCleanup)return true;
    if(!effects)return effect==ExpressionEvaluation::kRequired;
    switch(completion) {
      case FlowCompletion::kThrow:case FlowCompletion::kResumeThrow:
        return HasLanguageEffect(effects->before_throw,analysis::LanguageEffect::kUserCode);
      case FlowCompletion::kNormal:case FlowCompletion::kReturn:case FlowCompletion::kJump:
      case FlowCompletion::kTaken:case FlowCompletion::kNotTaken:case FlowCompletion::kResumeNext:
        return HasLanguageEffect(effects->normal,analysis::LanguageEffect::kUserCode);
      case FlowCompletion::kResumeReturn:
        return HasLanguageEffect(effects->normal|effects->before_throw,analysis::LanguageEffect::kUserCode);
      case FlowCompletion::kConservativeCleanup:return true;
    }
    throw TreeStructureError::kOwnership;
  }
};
struct FlowEdge {
  FlowPoint from, to;
  FlowCompletion completion;
  std::optional<std::size_t> incoming, outgoing;
};
// Temporary migration adapter, explicitly not the canonical semantic input.
// Effect summaries bootstrap conservatively and refine from shared flow
// initialization and original normal-value facts. The full operation/completion
// projection is still a migration gap; this view must not duplicate ISA rules.
struct LegacyFlowProjection {
  std::span<const std::optional<SymbolId>> aliases;
  std::span<const std::optional<PrimitiveId>> literals;
  std::span<const std::optional<ExprId>> replacements;
  std::span<const std::uint8_t> omitted;
  std::span<const ExpressionEvaluation> evaluations;
  std::span<const std::uint8_t> normal_stores;
  std::span<const std::optional<BindingInitializer>> initializers;
  std::span<const std::uint8_t> private_bindings;
  // Applied storage plan, distinct from immutable value aliases. All reads,
  // successful definitions and update read/write halves use this identity.
  std::span<const std::optional<SymbolId>> storage;
  std::span<const std::optional<SymbolId>> undefined_stores;
  std::span<const std::optional<DeclarationValue>> declaration_values;
  std::span<const std::optional<DeclarationCompletion>> declaration_completions;
  SourceScopeProjection source_scopes;
  std::span<const std::optional<PrimitiveId>> read_literals;
  std::span<const std::optional<ExpressionTransport>> transports;
  // Trial proposal identity at replacement reads. Nested transported RHSs
  // inherit the outer proposal; independent occurrences keep distinct roots.
  std::span<const std::optional<SymbolId>> transported_reads;
  // ReadValues addresses canonical slots after alias/storage publication.
  std::span<const std::optional<SymbolId>> read_bindings;
  std::span<const std::uint8_t> stable_entries;
  // Exact-projection normal completion values: result equals the current
  // binding at commit, not an earlier snapshot. Empty for unproved trials.
  std::span<const std::optional<SymbolId>> normal_bindings;
  std::span<const std::optional<SymbolId>> read_sources;
  std::span<const std::optional<ExprId>> statement_values;
  std::span<const analysis::ValueKinds> normal_kinds;
  const OriginalFlow* original=nullptr;
  // Borrowed only during construction. Current/trial share the grammar
  // provider; returned flow owns phase points, never this syntax pointer.
  const LoopSyntax* loops=nullptr;
  // Borrow immutable canonical access/capture facts, not another proposal's
  // scalar, liveness or initialization product. Returned flow copies access.
  const SourceCells* cells=nullptr;
  // Syntax facets whose execution is carried by other projected occurrences.
  // Their old statement boundary must still revoke its raw completion proof.
  std::span<const std::optional<ExprId>> forwarded_values;
  std::span<const std::optional<std::uint32_t>> loop_declarations;
  [[nodiscard]] std::optional<PrimitiveId> ReadLiteral(ExprId expression,SymbolId binding) const {
    if(!read_literals.empty()&&read_literals[expression.value])return read_literals[expression.value];
    return literals[binding.value];
  }
};
// Actual source loop completion phases. Entry/repetition are distinct even
// when they carry the same scalar values; iteration cell transport is separate.
struct FlowLoop {
  FlowPoint condition,body,increment,exit;
  // Completed scalar copying, not a certificate of captured-cell identity.
  std::optional<FlowPoint> entry_copy,repeat_copy;
  std::optional<FlowPoint> entry_allocation,repeat_allocation;
};
struct RecoveryFlow {
  std::vector<std::optional<FlowIteration>> iterations; // StmtId indexed.
  std::vector<FlowIterationClose> iteration_closes;
  std::vector<FlowParameterDefault> parameter_defaults;
  // The common lifetime/activation owner decides current-unit capability.
  // Consumers must not repeat a scope/loop shape test. Partial point contexts
  // remain unknown at TransferBinding; a known site alone never equates cells.
  [[nodiscard]] FlowUnitCapability BindingUnit(SymbolId binding) const {
    if(binding.value>=bindings.size())return scopes?FlowUnitCapability::kUnmodeled:FlowUnitCapability::kCompletionOnly;
    return std::visit([&](const auto& unit) {
      using T=std::decay_t<decltype(unit)>;
      if constexpr(std::is_same_v<T,EnclosingBindingLifetime>)return FlowUnitCapability::kCurrent;
      else if constexpr(std::is_same_v<T,CompletionOnlyLifetime>)return FlowUnitCapability::kCompletionOnly;
      else if constexpr(std::is_same_v<T,UnmodeledBindingLifetime>)return FlowUnitCapability::kUnmodeled;
      else if constexpr(std::is_same_v<T,ScopeBindingLifetime>) {
        return scopes&&scopes->events.size()==events.size()&&unit.scope.value<scopes->regions.size()&&
            scopes->regions[unit.scope.value].first<scopes->regions[unit.scope.value].last?
            FlowUnitCapability::kCurrent:FlowUnitCapability::kUnmodeled;
      } else {
        static_assert(std::is_same_v<T,LoopBindingLifetime>);
        return scopes&&scopes->loop_events.size()==events.size()&&unit.loop.value<scopes->loop_regions.size()&&
            scopes->loop_regions[unit.loop.value].first<scopes->loop_regions[unit.loop.value].last?
            FlowUnitCapability::kCurrent:FlowUnitCapability::kUnmodeled;
      }
    },bindings[binding.value].lifetime);
  }
  // Initialization and transfer use the same unit facts. A successful store
  // alone does not prove that a lexical unit exists at a later occurrence.
  // Completion-only graphs retain their explicit non-lexical contract.
  [[nodiscard]] FlowUnitPresence BindingPresence(SymbolId binding,FlowPoint point,FlowScopePhase phase) const {
    switch(BindingUnit(binding)) {
      case FlowUnitCapability::kUnmodeled:return FlowUnitPresence::kUnmodeled;
      case FlowUnitCapability::kCompletionOnly:return FlowUnitPresence::kCompletionOnly;
      case FlowUnitCapability::kCurrent:break;
    }
    const auto& lifetime=bindings[binding.value].lifetime;
    if(const auto* unit=std::get_if<ScopeBindingLifetime>(&lifetime))return scopes->Presence(unit->scope,point,phase);
    if(const auto* unit=std::get_if<LoopBindingLifetime>(&lifetime))return scopes->LoopPresence(unit->loop,point,phase);
    return FlowUnitPresence::kActive;
  }
  // Shared unit transfer owns both scope events and abrupt edge exits.
  // Unmodeled transfer preserves possible demand but invalidates known values.
  [[nodiscard]] FlowUnitTransfer TransferBinding(SymbolId binding,const FlowEdge& edge) const {
    // Value/completion-only inputs carry no lexical unit certificate. Demand
    // remains valid; missing source lifetime facts cannot establish equality.
    if(binding.value>=bindings.size())return scopes?FlowUnitTransfer::kUnmodeled:FlowUnitTransfer::kPreserved;
    const auto capability=BindingUnit(binding);
    if(capability==FlowUnitCapability::kUnmodeled)return FlowUnitTransfer::kUnmodeled;
    if(ResetsBinding(binding,edge.from))return FlowUnitTransfer::kEnded;
    if(capability==FlowUnitCapability::kCompletionOnly)return FlowUnitTransfer::kPreserved;
    // Capability belongs to the unchanged graph, not either edge endpoint.
    // Check it once, then use the common scope transfer instead of rebuilding
    // the same presence/capability decisions for both endpoints.
    const auto& lifetime=bindings[binding.value].lifetime;
    if(const auto* unit=std::get_if<ScopeBindingLifetime>(&lifetime))return scopes->Transfer(unit->scope,edge.from,edge.to);
    if(const auto* unit=std::get_if<LoopBindingLifetime>(&lifetime))return scopes->LoopTransfer(unit->loop,edge.from,edge.to);
    return FlowUnitTransfer::kPreserved; // Current enclosing invocation unit.
  }
  [[nodiscard]] bool ResetsBinding(SymbolId binding,FlowPoint point) const {
    if(binding.value>=bindings.size())return false;
    if(bindings[binding.value].allocation==point)return true;
    if(const auto* unit=std::get_if<LoopBindingLifetime>(&bindings[binding.value].lifetime)) {
      const auto& event=events[point.value];
      return event.loop_environment==unit->loop&&event.ResetsLoopUnit();
    }
    const auto* unit=std::get_if<ScopeBindingLifetime>(&bindings[binding.value].lifetime);
    if(!unit)return false;
    const auto& event=events[point.value];
    return event.scope==unit->scope&&event.ResetsScopeUnit();
  }
  // Borrowed from the prepared owner, which outlives all revision-owned facts.
  // The original graph is immutable; source projections never rewrite it.
  const OriginalFlow* original=nullptr;
  std::shared_ptr<const FlowScopes> scopes;
  std::vector<analysis::ValueKinds> normal_kinds;
  // Exact-proposal control domains from the joint solve, never borrowed from
  // a committed/candidate predecessor. Graph construction clears old facts.
  std::vector<TruthDomains> normal_truths;
  std::vector<std::optional<FlowPoint>> normal_exits;
  // ExprId-indexed raw-phase bridge. Ordinary/source-only graphs consume their
  // existing native events, properties and completion edges without this copy.
  std::vector<std::optional<FlowAction>> actions;
  std::vector<FlowAggregate> aggregates;
  std::vector<FlowArgumentCarrier> argument_carriers;
  std::vector<FlowSnapshotSlot> snapshot_slots;
  std::vector<std::optional<FlowAction>> property_initializers; // PropertyId indexed.
  std::vector<std::optional<FlowEnumeration>> enumerations; // StmtId indexed.
  std::vector<std::optional<FlowOriginalAction>> original_actions; // OpId indexed.
  std::vector<FlowInvocationValue> invocation_values;
  std::vector<std::optional<ParameterId>> original_invocations; // OpId indexed.
  std::vector<std::optional<FlowAction>> original_regions; // OriginalActionRegionId indexed.
  // Verified raw phase identities projected once by the source adapter.
  // Absent entries are representation gaps; neither PCs nor edge order infer
  // an action, success-only result or pending-exception entry.
  std::vector<std::optional<FlowOriginalPhase>> original_phases; // WorkBlockId indexed.
  // Exit and handler facts are produced once from checked raw identities and
  // native completion contexts. Catch caps consume this closure in O(1), not
  // a per-definition walk through the original graph or a layout predicate.
  std::vector<std::optional<FlowOriginalFailure>> original_exceptions; // Exceptional exit indexed.
  std::vector<FlowOriginalRouteCoverage> original_handlers; // WorkBlockId indexed.
  std::vector<std::optional<FlowOriginalResult>> original_results; // OpId indexed.
  std::vector<std::optional<FlowOriginalControl>> original_controls; // OpId indexed.
  // Current physical references to verified cells, allocated only for owners
  // with raw cell correspondence. Value aliases do not grant unit identity.
  std::vector<std::optional<FlowReferenceId>> original_cell_references; // OpId indexed.
  std::vector<std::optional<FlowOriginalUnit>> original_cell_units; // CellId indexed.
  std::vector<std::optional<SourceEffects>> original_effects; // ExprId indexed, intrinsic only.
  RouteInputIdentity identity;
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  std::vector<FlowEvent> events;
  std::vector<FlowEdge> edges;
  std::vector<FlowDefinition> definitions;
  std::vector<FlowCapturedInput> captured_inputs;
  std::vector<FlowReference> references;
  std::vector<FlowUnitCapture> unit_captures;
  bool unit_captures_complete=false;
  std::vector<FlowPropertyReference> properties;
  std::vector<FlowNameReference> names;
  std::vector<DiscardedEvaluation> discarded;
  std::vector<FlowPoint> statements, expressions;
  std::vector<std::optional<FlowLoop>> loops;
  // Retained ownership forest, in parent-before-child order. Omitted syntax
  // has no record. Consumers fold once, never walk a graph per statement.
  std::vector<std::optional<FlowStatement>> statement_semantics;
  std::vector<StmtId> statement_order;
  // Actual owners after all expression projections, including transported
  // RHSs. Unmodeled occurrences remain absent; old AST owners are not proof.
  std::vector<std::optional<StmtId>> expression_owners;
  // Optional trial-only provenance. This records transformation dependence,
  // not binding equivalence or original SSA identity; empty outside a trial.
  std::vector<std::optional<SymbolId>> expression_transports;
  // Current expression occurrences, including replacements. Unmodeled signature
  // roots are not evidence of an ignored value. Effects remain independent.
  std::vector<FlowValueUse> value_uses;
  std::vector<FlowBinding> bindings;
  std::vector<SourceRegion> source_regions;
  std::vector<SourceBindingShadow> source_shadows;
  std::vector<SourceBindingPresence> binding_presence;
  std::vector<SourceSpellingId> source_spellings;
  std::vector<std::optional<SourceSpellingId>> source_name_spellings;
  std::optional<SourceNameIndex> source_names;
  std::vector<std::optional<SourcePosition>> source_occurrences;
  FlowPoint entry, terminal;
  std::size_t allocated_bytes = 0;
  // Conservative owned storage after the builder is destroyed, including
  // transferred capacity. allocated_bytes measures new construction admission.
  std::size_t retained_bytes = 0;
  std::uint64_t work = 0;
  // The syntax adapter publishes original and projected lexical regions once.
  // Absence on an original binding preserves the legacy identity contract;
  // neither this query nor containment proves spelling, completion or cell identity.
  [[nodiscard]] std::optional<SourceRegionId> BindingRegion(SymbolId binding) const {
    return std::visit([](const auto& scope)->std::optional<SourceRegionId> {
      using T=std::decay_t<decltype(scope)>;
      if constexpr(std::is_same_v<T,OriginalBindingScope>||std::is_same_v<T,ProjectedBindingScope>)return scope.region;
      else {static_assert(std::is_same_v<T,UnmodeledBindingScope>||std::is_same_v<T,EnclosingBindingScope>);return {};}
    },bindings[binding.value].scope);
  }
  [[nodiscard]] bool BindingInScope(SymbolId binding,std::optional<StmtId> occurrence) const {
    if(const auto region_id=BindingRegion(binding)) {
      if(!occurrence || !source_occurrences[occurrence->value])return false;
      const auto position=*source_occurrences[occurrence->value];const auto region=source_regions[region_id->value];
      return position>=region.first&&position<region.last;
    }
    return std::holds_alternative<OriginalBindingScope>(bindings[binding.value].scope)||
        std::holds_alternative<EnclosingBindingScope>(bindings[binding.value].scope);
  }
};
// Recomputed for every recovery proposal, including retained forwarded routes.
void VerifyForOfRecovery(const FunctionTree&,RecoveryFlow&,TreeCheckBudget&);
void VerifyForInRecovery(const FunctionTree&, RecoveryFlow&, TreeCheckBudget&);
[[nodiscard]] std::variant<TreeStorage,TreeStructureError> VerifyInitialForOfRecovery(const FunctionTree&,const OriginalFlow&,analysis::SemanticBudget = {});
[[nodiscard]] std::variant<TreeStorage,TreeStructureError> VerifyInitialForInRecovery(
    const FunctionTree&,const OriginalFlow&,analysis::SemanticBudget = {});
[[nodiscard]] std::optional<FlowPoint> FindOriginalPhase(const RecoveryFlow&,
    analysis::WorkBlockId,analysis::ExceptionPhase,std::optional<analysis::OpId> = {});
[[nodiscard]] std::optional<FlowOriginalControl> FindOriginalControlPhase(const RecoveryFlow&,
    analysis::WorkBlockId,analysis::ExceptionPhase,analysis::OpId);
[[nodiscard]] std::optional<OriginalResult> FindFlowOriginalResult(const RecoveryFlow&,analysis::OpId);
[[nodiscard]] std::optional<FlowOriginalControl> FindFlowOriginalControl(const RecoveryFlow&,analysis::OpId);
[[nodiscard]] const FlowReference* FindFlowOriginalCellReference(const RecoveryFlow&,analysis::OpId);
[[nodiscard]] std::optional<FlowOriginalResult> FindFlowOriginalGuard(const RecoveryFlow&,analysis::OpId);
// Qualified virtual facts for pure raw components. Input completion belongs
// to the captured source occurrence; domains carry each component's path.
// Boolean results remain distinct from the native selection result/action.
[[nodiscard]] std::optional<FlowControlDomains> OriginalControlDomains(
    const RecoveryFlow&,analysis::OpId,std::span<const TruthDomains> = {});
[[nodiscard]] NormalValueDomain OriginalControlResultDomains(
    const RecoveryFlow&,analysis::OpId,std::span<const TruthDomains> = {});
[[nodiscard]] std::optional<analysis::OpId> OriginalDefinitionControl(
    const RecoveryFlow&,const OriginalSourceDefinition&);
enum class FlowOriginalProofState : std::uint8_t {kUnavailable,kPending,kUnreachable,kAvailable};
struct FlowOriginalDefinitionProof {
  FlowOriginalProofState state=FlowOriginalProofState::kUnavailable;
  analysis::ValueKinds kinds;
};
// Bottom in an existing control input is a pending equation, not absent
// correspondence. Consumers must wait/replay instead of widening too early.
[[nodiscard]] FlowOriginalDefinitionProof OriginalDefinitionProof(const RecoveryFlow&,
    const OriginalSourceDefinition&,std::span<const TruthDomains> = {});
[[nodiscard]] analysis::ValueKinds OriginalDefinitionKinds(const RecoveryFlow&,const OriginalSourceDefinition&,
    std::span<const TruthDomains> = {});
[[nodiscard]] analysis::ValueKinds OriginalCatchKinds(const RecoveryFlow&,const OriginalCatchBinding&);
struct FlowBindingNames {
  std::vector<SourceBindingPresence> presence;
  std::vector<SourceBindingShadow> shadows;
  std::optional<SourceNameIndex> index;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Analyze one current graph. Definitions/references and observable cells retain
// lexical names; canonical same-spelling parents skip already-elided bindings.
// This never chooses storage or authorizes removing a declaration/TDZ effect.
[[nodiscard]] std::variant<FlowBindingNames,TreeStructureError> AnalyzeFlowBindingNames(
    const RecoveryFlow&,std::span<const SourceBindingShadow>,analysis::SemanticBudget = {});
// Source-side migration entry: checked tree plus explicit legacy projection.
// Parameter definitions denote body entry after the existing signature program.
// Scope leave events currently describe normal exits only; abrupt unwind and
// capture-cell events must be added before using this graph to move lifetimes.
// Defines control/completion and successful definition events once. Consumers
// may analyze these facts; this routine neither selects storage nor elides code.
// Original IR correspondence retention and canonical binding lifetime analysis
// remain separate required steps of unified-variables.md, not implied here.
// Optional storage transfers already-charged buffers from an invalidated graph
// owned by the same preparation budget. No facts survive; only capacity does.
[[nodiscard]] std::variant<RecoveryFlow, TreeStructureError> BuildRecoveryFlow(
    const FunctionTree&, LegacyFlowProjection, analysis::SemanticBudget = {},RecoveryFlow storage = {});
// Read-only facts for one projection. A worklist may query one changed event
// without mutating graph links, discarded groups, or another analysis product.
struct FlowEffectInput {
  std::span<const ExpressionEvaluation> evaluations;
  std::span<const std::uint8_t> normal_stores;
  std::span<const ExpressionEvaluation> assignments;
  std::span<const analysis::ValueKinds> normal_kinds;
  // Solver-owned changing input domains. Empty consumes the published facts
  // of this graph; it must never borrow a previous projection's input product.
  std::span<const analysis::ValueKinds> input_kinds;
};
struct FlowEventEffects {
  ExpressionEvaluation retention=ExpressionEvaluation::kInert;
  std::optional<SourceEffects> intrinsic;
};
[[nodiscard]] FlowEventEffects EvaluateFlowEventEffects(const RecoveryFlow&,
    const FunctionTree&,const FlowEvent&,FlowEffectInput);
struct FlowRefinement { std::size_t allocated_bytes=0; std::uint64_t work=0; };
// Refine a conservative graph's binding effects after initialization proof.
// Event/definition/reference identities are preserved. An exact-projection
// executable mask may additionally remove proved inactive completions. Never
// compact edges before the joint initialization/value/provenance solve ends.
// The supplied summaries must describe this unchanged projection, not a plan
// mutation. Demand/liveness must be computed after this refinement.
[[nodiscard]] std::variant<FlowRefinement,TreeStructureError> RefineRecoveryFlowEffects(
    RecoveryFlow&,const FunctionTree&,std::span<const ExpressionEvaluation>,
    std::span<const std::uint8_t> normal_stores,analysis::SemanticBudget = {},
    std::span<const std::uint8_t> executable = {});
void VerifyProtocolForwardingRecovery(const FunctionTree&,RecoveryFlow&,TreeCheckBudget&);
}
#endif
