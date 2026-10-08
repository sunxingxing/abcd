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
struct OriginalBindingScope {};
struct ProjectedBindingScope {SourceRegionId region;};
struct UnmodeledBindingScope {};
// Lexical containment only: dynamic cell identity, initialization completion
// and source spelling remain separate obligations. Missing projection facts
// cannot authorize access to a narrowed declaration.
using SourceBindingScope=std::variant<OriginalBindingScope,ProjectedBindingScope,UnmodeledBindingScope>;
struct SourceScopeProjection {
  std::span<const SourceRegion> regions;
  std::span<const std::optional<SourcePosition>> occurrences;
  std::span<const SourceBindingScope> bindings;
};
struct FlowBinding {
  BindingKind kind;
  FlowBindingExposure exposure = FlowBindingExposure::kObservable;
  std::optional<StmtId> declaration;
  std::optional<FlowDefinitionKind> initialization;
  SourceBindingScope scope=OriginalBindingScope{};
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
  std::vector<std::optional<SourcePosition>> source_occurrences;
  FlowPoint entry, terminal;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  // Supplemental containment for recovered declarations. Original scope has
  // no added narrowing; ordinary name resolution still must succeed. This is
  // not a completion-dominance or per-activation/capture identity proof.
  [[nodiscard]] bool BindingInScope(SymbolId binding,std::optional<StmtId> occurrence) const {
    return std::visit([&](const auto& scope) {
      using T=std::decay_t<decltype(scope)>;
      if constexpr(std::is_same_v<T,OriginalBindingScope>)return true;
      else if constexpr(std::is_same_v<T,UnmodeledBindingScope>)return false;
      else {
        static_assert(std::is_same_v<T,ProjectedBindingScope>);
        if(!occurrence || !source_occurrences[occurrence->value])return false;
        const auto position=*source_occurrences[occurrence->value];const auto region=source_regions[scope.region.value];
        return position>=region.first&&position<region.last;
      }
    },bindings[binding.value].scope);
  }
};
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
