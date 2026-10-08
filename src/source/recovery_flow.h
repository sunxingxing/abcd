#ifndef ABCD_SOURCE_RECOVERY_FLOW_H_
#define ABCD_SOURCE_RECOVERY_FLOW_H_
#include "expression_transport.h"
#include "tree_structure.h"
#include "expression_observation.h"
#include "primitive_value.h"
#include <type_traits>
namespace abcd::source {
struct FlowPoint { std::uint32_t value = 0; auto operator<=>(const FlowPoint&) const = default; };
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
enum class FlowDefinitionKind : std::uint8_t { kParameter, kUndefined, kInitializer, kAssignment, kUpdate, kCatch, kUndefinedStore, kFunctionSelf };
enum class FlowEventKind : std::uint8_t { kControl, kTruthBranch, kNullishBranch, kRead, kEvaluation, kEnterScope, kLeaveScope, kDefinition, kSuspend };
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
struct SourceScopeProjection {
  std::span<const SourceRegion> regions;
  std::span<const std::optional<SourcePosition>> occurrences;
  std::span<const SourceBindingScope> bindings;
  std::span<const SourceBindingShadow> shadows;
  std::span<const std::uint8_t> unique_spellings;
  std::span<const SourceSpellingId> spellings;
  std::span<const std::optional<SourceSpellingId>> names;
};
struct FlowBinding {
  BindingKind kind;
  FlowBindingExposure exposure = FlowBindingExposure::kObservable;
  std::optional<StmtId> declaration;
  std::optional<FlowDefinitionKind> initialization;
  SourceBindingScope scope=OriginalBindingScope{};
  bool unique_spelling=true;
  // Semantic adapter proof: body-entry identity cannot be changed by mapped
  // arguments, unknown captures or unmodeled signature evaluation. The value
  // solver independently rejects retained writes before grounding this root.
  bool stable_entry=false;
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
};
// Free lexical names are observations, not bindings with local definitions.
// Their original expression and current owner are distinct after forwarding.
struct FlowNameReference {
  FlowPoint point;
  NameId name;
  ExprId site;
  std::optional<StmtId> owner;
};
struct BindingInitializer;
struct DeclarationValue;
struct DeclarationCompletion;
struct FlowDefinition {
  FlowPoint point;
  SymbolId binding;
  FlowDefinitionKind kind;
  std::optional<SymbolId> copy;
  std::optional<ExprId> value;
  std::optional<StmtId> declaration;
  std::optional<ExprId> write;
  FlowWriteEffect effect = FlowWriteEffect::kObservable;
  // The semantic adapter owns both expression and statement write positions.
  // Kind still distinguishes lexical completion from assignment/slot writes;
  // a site alone never authorizes deleting initialization or binding lifetime.
  [[nodiscard]] std::optional<FlowReferenceSite> WriteSite() const {
    if(write)return FlowReferenceSite{*write};
    if(declaration)return FlowReferenceSite{*declaration};
    return std::nullopt;
  }
};
struct FlowEvent {
  FlowEventKind kind = FlowEventKind::kControl;
  std::optional<SymbolId> read;
  std::optional<FlowDefinitionId> definition;
  std::optional<ExprId> expression;
  std::optional<ScopeId> scope;
  std::optional<std::size_t> incoming, outgoing;
  std::optional<DiscardedId> discarded;
  // Intrinsic effect at this event, refined with binding completion. State
  // reads and successful writes remain observations even when this is inert.
  ExpressionEvaluation effect = ExpressionEvaluation::kInert;
};
struct FlowEdge {
  FlowPoint from, to;
  FlowCompletion completion;
  std::optional<std::size_t> incoming, outgoing;
};
// Temporary migration adapter, explicitly not the canonical semantic input.
// Effect summaries bootstrap conservatively and refine from shared flow
// initialization facts. Original-IR correspondence remains a migration gap;
// this view must not become a second source of truth for new optimizations.
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
  [[nodiscard]] std::optional<PrimitiveId> ReadLiteral(ExprId expression,SymbolId binding) const {
    if(!read_literals.empty()&&read_literals[expression.value])return read_literals[expression.value];
    return literals[binding.value];
  }
};
struct RecoveryFlow {
  RouteInputIdentity identity;
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  std::vector<FlowEvent> events;
  std::vector<FlowEdge> edges;
  std::vector<FlowDefinition> definitions;
  std::vector<FlowReference> references;
  std::vector<FlowNameReference> names;
  std::vector<DiscardedEvaluation> discarded;
  std::vector<FlowPoint> statements, expressions;
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
