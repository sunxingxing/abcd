#ifndef ABCD_SOURCE_RECOVERY_FLOW_H_
#define ABCD_SOURCE_RECOVERY_FLOW_H_
#include "expression_transport.h"
#include "tree_structure.h"
#include "expression_observation.h"
#include "primitive_value.h"
#include "original_flow.h"
#include "flow_scopes.h"
#include "cell_access.h"
#include <type_traits>
namespace abcd::source {
struct FlowPoint { std::uint32_t value = 0; auto operator<=>(const FlowPoint&) const = default; };
// Checked raw result and its current source evaluation interval. The normal
// continuation excludes the throwing route and never certifies a later store.
// Missing entries are correspondence gaps, not permission to reuse raw effects.
struct FlowOriginalResult { ExprId expression; FlowPoint entry,normal; };
struct FlowDefinitionId { std::uint32_t value = 0; auto operator<=>(const FlowDefinitionId&) const = default; };
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
enum class FlowDefinitionKind : std::uint8_t { kParameter, kUndefined, kInitializer, kAssignment, kUpdate, kCatch, kUndefinedStore, kFunctionSelf, kIterationCopy };
enum class FlowEventKind : std::uint8_t { kControl, kTruthBranch, kNullishBranch, kRead, kEvaluation, kEnterScope, kLeaveScope, kDefinition, kSuspend, kPropertyReference, kEnterLoopEnvironment, kCopyInput, kEnterIterationEnvironment, kLeaveLoopEnvironment };
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
struct CompletionOnlyLifetime {};
struct EnclosingBindingLifetime {};
struct UnmodeledBindingLifetime {};
struct ScopeBindingLifetime { ScopeId scope; FlowPoint enter,leave; };
struct LoopBindingLifetime {StmtId loop;};
using FlowBindingLifetime=std::variant<CompletionOnlyLifetime,EnclosingBindingLifetime,ScopeBindingLifetime,UnmodeledBindingLifetime,LoopBindingLifetime>;
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
// demand/value consumers must preserve the old-value dependency across resets.
struct FlowCapturedValue {FlowPoint point;SymbolId binding;auto operator<=>(const FlowCapturedValue&)const=default;};
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
  FlowProvenance provenance=FlowProvenance::kUnmodeled;
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
  // Shared unit transfer owns both scope events and abrupt edge exits.
  // Unmodeled transfer preserves possible demand but invalidates known values.
  [[nodiscard]] FlowUnitTransfer TransferBinding(SymbolId binding,const FlowEdge& edge) const {
    // Value/completion-only inputs carry no lexical unit certificate. Demand
    // remains valid; missing source lifetime facts cannot establish equality.
    if(binding.value>=bindings.size())return scopes?FlowUnitTransfer::kUnmodeled:FlowUnitTransfer::kPreserved;
    if(BindingUnit(binding)==FlowUnitCapability::kUnmodeled)return FlowUnitTransfer::kUnmodeled;
    if(ResetsBinding(binding,edge.from))return FlowUnitTransfer::kEnded;
    if(const auto* unit=std::get_if<LoopBindingLifetime>(&bindings[binding.value].lifetime))
      return scopes?scopes->LoopTransfer(unit->loop,edge.from,edge.to):FlowUnitTransfer::kUnmodeled;
    const auto* unit=std::get_if<ScopeBindingLifetime>(&bindings[binding.value].lifetime);
    return unit&&scopes?scopes->Transfer(unit->scope,edge.from,edge.to):FlowUnitTransfer::kPreserved;
  }
  [[nodiscard]] bool ResetsBinding(SymbolId binding,FlowPoint point) const {
    if(binding.value>=bindings.size())return false;
    if(bindings[binding.value].allocation==point)return true;
    if(const auto* unit=std::get_if<LoopBindingLifetime>(&bindings[binding.value].lifetime)) {
      const auto& event=events[point.value];
      return event.loop_environment==unit->loop && (event.kind==FlowEventKind::kEnterLoopEnvironment||event.kind==FlowEventKind::kEnterIterationEnvironment||event.kind==FlowEventKind::kLeaveLoopEnvironment);
    }
    const auto* unit=std::get_if<ScopeBindingLifetime>(&bindings[binding.value].lifetime);
    if(!unit)return false;
    const auto& event=events[point.value];
    return event.scope==unit->scope &&
        (event.kind==FlowEventKind::kEnterScope||event.kind==FlowEventKind::kLeaveScope);
  }
  // Borrowed from the prepared owner, which outlives all revision-owned facts.
  // The original graph is immutable; source projections never rewrite it.
  const OriginalFlow* original=nullptr;
  std::shared_ptr<const FlowScopes> scopes;
  std::vector<analysis::ValueKinds> normal_kinds;
  std::vector<std::optional<FlowPoint>> normal_exits;
  std::vector<std::optional<FlowOriginalResult>> original_results; // OpId indexed.
  RouteInputIdentity identity;
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  std::vector<FlowEvent> events;
  std::vector<FlowEdge> edges;
  std::vector<FlowDefinition> definitions;
  std::vector<FlowReference> references;
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
struct FlowRefinement { std::size_t allocated_bytes=0; std::uint64_t work=0; };
// Refine a conservative graph's binding effects after initialization proof.
// Event/definition/reference identities are preserved; only exception edges
// may disappear. Positive completion-dominance facts therefore remain sound.
// The supplied summaries must describe this unchanged projection, not a plan
// mutation. Demand/liveness must be computed after this refinement.
[[nodiscard]] std::variant<FlowRefinement,TreeStructureError> RefineRecoveryFlowEffects(
    RecoveryFlow&,const FunctionTree&,std::span<const ExpressionEvaluation>,
    std::span<const std::uint8_t> normal_stores,analysis::SemanticBudget = {});
}
#endif
