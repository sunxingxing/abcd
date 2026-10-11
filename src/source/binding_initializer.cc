#include "source_names.h"
#include "source_name_retirement.h"
#include "binding_initializer.h"
#include "declaration_retirement.h"
#include "snapshot_projection.h"
#include "original_flow.h"
#include "expression_observation.h"
#include "recovery_schedule.h"
#include "recovery_projection.h"
#include "binding_interference.h"
#include "storage_coalescing.h"
#include "definition_elision.h"
#include "flow_dominance.h"
#include "flow_definitions.h"
#include "flow_constants.h"
#include "flow_copies.h"
#include "flow_statements.h"
#include "primitive_value.h"
#include "flow_motion.h"
#include "class_tree.h"
#include "expression_children.h"
#include "loop_syntax.h"
#include "loop_declarations.h"
#include "loop_entry_projection.h"
#include "nullish_syntax.h"
#include "statement_children.h"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
struct BindingUses {
  std::optional<StmtId> declaration, legacy_initializer;
  std::optional<ExprId> first;
  std::optional<std::size_t> first_container, last_container;
  bool escaped = false, outside_body = false;
  bool private_storage = false, module_alias = false, named_creation = false;
};
struct DeclarationSite { StmtId statement, container; };
struct DeclarationEdit { DeclarationSite site; BindingInitializer initializer; };
struct ContainerRange { std::size_t begin = 0, end = 0; };
struct ProjectionObservation { SymbolId symbol; StmtId owner; };
struct ExpressionVisit {
  ExprId expression;
  bool finish = false;
};
struct StatementVisit {
  StmtId statement;
  std::optional<StmtId> container;
  std::optional<StmtId> lexical;
  std::optional<StmtId> projector;
  std::size_t child = 0;
  bool projected = false;
  bool entered = false;
  ScopeId scope;
  std::optional<StmtId> entry;
};
class Initializers {
 public:
  Initializers(const FunctionTree& tree, const LoopSyntax& loops, const NullishSyntax& nullish,
      TreeCheckBudget& budget,const OriginalFlow* original) : tree_(tree), loops_(loops), nullish_(nullish), budget_(budget), original_(original) {
    if(original_)CheckOriginalFlow(tree_,*original_);
    cells_=AnalyzeSourceCells(tree_,budget_);
    budget.Array(plan_.statements, tree.statements.size()); budget.Array(plan_.omitted, tree.statements.size());
    budget.Array(plan_.statement_values,tree.statements.size());
    budget.Array(plan_.loop_declarations, tree.statements.size());
    budget.Array(plan_.forwarded_values, tree.statements.size());
    budget.Array(plan_.literals, tree.symbols.size());
    budget.Array(plan_.read_literals, tree.expressions.size());
    budget.Array(plan_.read_bindings,tree.symbols.size());
    budget.Array(plan_.read_sources,tree.expressions.size());
    budget.Array(plan_.aliases, tree.symbols.size());
    budget.Array(plan_.storage, tree.symbols.size());
    budget.Array(plan_.undefined_stores, tree.statements.size());
    budget.Array(plan_.declaration_values, tree.statements.size());
    budget.Array(plan_.declaration_completions, tree.statements.size());
    budget.Array(plan_.replacements, tree.expressions.size());
    budget.Array(plan_.transports, tree.expressions.size());
    budget.Array(completions_, tree.expressions.size());
    budget.Array(plan_.entry_counts, loops.initializer_roots.size() + 1);
    budget.Array(plan_.child_counts, tree.children.size() + 1);
    budget.Array(container_parents_, tree.statements.size());
    budget.Array(container_order_, tree.statements.size());
    budget.Array(constant_heads_, tree.symbols.size());budget.Array(constant_next_, tree.symbols.size());
    budget.Array(constant_queue_, tree.symbols.size());
    budget.Array(uses_, tree.symbols.size()); budget.Array(projected_, tree.statements.size());
    budget.Array(original_declarations_,tree.symbols.size());
    budget.Array(declaration_sites_,tree.symbols.size());
    budget.Array(declaration_containers_,tree.statements.size());
    budget.Array(containers_, tree.statements.size());
    budget.Array(lexical_, tree.statements.size()); budget.Array(projectors_, tree.statements.size());
    budget.Array(retained_bodies_, tree.statements.size());
    budget.Array(projection_observations_, ProjectionCapacity());
    budget.Array(expressions_, tree.expressions.size()); budget.Array(statements_, tree.statements.size());
    budget.Array(statement_owners_,tree.statements.size());
    budget.Array(source_regions_,tree.statements.size());budget.Array(source_occurrences_,tree.statements.size());
    budget.Array(source_scope_regions_,tree.scopes.size());
    budget.Array(source_allocations_,tree.statements.size());
    budget.Array(loop_entry_occurrences_,tree.statements.size());
    budget.Array(canonical_scopes_,tree.statements.size());
    budget.Array(expression_owners_,tree.expressions.size());budget.Array(expression_projections_,tree.expressions.size());
  }
  BindingInitializers Run() {
    if (!tree_.recovery_issues.empty()) {
      // Unknown control prevents semantic simplification, but printer clients
      // still require occurrence-indexed summaries. Publish conservative facts
      // on this terminal path too; an absent array is not an unknown effect.
      budget_.Array(plan_.evaluations,tree_.expressions.size());
      budget_.Array(plan_.normal_stores,tree_.expressions.size());
      return Finish();
    }
    IndexLiteralTexts();
    IndexPrimitiveValues();
    IndexValueEquations();
    for(std::size_t i=0;i<uses_.size();++i){budget_.Step();uses_[i].escaped=cells_.access[i]!=CellVisibility::kClosed;}
    for (const auto& alias : tree_.modules) {
      budget_.Step(); uses_[alias.symbol.value].module_alias = true;
    }
    for (const auto& parameter : tree_.parameters) {
      budget_.Step(); if (parameter.initializer) Scan(*parameter.initializer, {}, {});
    }
    // Expression programs cannot host statement declarations. Loop entry may
    // instead use ForDeclaration after the common completion/scope proof.
    // A loop expression owns a nested nullish statement's destination scope;
    // retain that outer owner regardless of statement arena numbering.
    for (std::size_t i = 0; i < tree_.statements.size(); ++i) {
      budget_.Step();
      if (loops_.initializer_owners[i]) { projected_[i] = 1; projectors_[i] = loops_.initializer_owners[i]; }
      if (!nullish_.statement_omitted_by.empty() && nullish_.statement_omitted_by[i]) {
        projected_[i] = 1; if (!projectors_[i]) projectors_[i] = nullish_.statement_omitted_by[i];
      }
      if (!nullish_.statement_projections.empty() && nullish_.statement_projections[i]) {
        projected_[i] = 1; if (!projectors_[i]) projectors_[i] = StmtId{static_cast<std::uint32_t>(i)};
      }
      if (const auto* loop = std::get_if<LoopStmt>(&tree_.statements[i].statement)) {
        if (const auto& condition = loops_.conditions[i]) {
          if (condition->body) retained_bodies_[condition->body->value] = StmtId{static_cast<std::uint32_t>(i)};
          const auto& block = std::get<BlockStmt>(tree_.statements[loop->body.value].statement);
          for (unsigned child = 0; child < condition->header_count; ++child) {
            budget_.Step(); const auto root = tree_.children[block.first_child + child];
            projected_[root.value] = 1; projectors_[root.value] = StmtId{static_cast<std::uint32_t>(i)};
          }
        }
        if (const auto& transition = loops_.transitions[i]) {
          const auto& block = std::get<BlockStmt>(tree_.statements[transition->block.value].statement);
          for (auto child = transition->first; child < transition->end; ++child) {
            budget_.Step(); const auto root = tree_.children[block.first_child + child];
            projected_[root.value] = 1; projectors_[root.value] = StmtId{static_cast<std::uint32_t>(i)};
          }
        }
      }
      if (const auto first = loops_.tail_starts[i]) {
        const auto& block = std::get<BlockStmt>(tree_.statements[i].statement);
        for (auto child = *first; child < block.child_count; ++child) {
          budget_.Step(); projected_[tree_.children[block.first_child + child].value] = 1;
        }
      }
    }
    std::size_t pending = 1; statements_[0] = {tree_.body};statements_[0].scope=tree_.body_scope;
    while (pending) {
      budget_.Step(); auto& visit = statements_[pending - 1];
      const auto& node = tree_.statements[visit.statement.value].statement;
      if (!visit.entered) {
        if(const auto* block=std::get_if<BlockStmt>(&node))visit.scope=block->scope;
        if(loops_.initializer_owners[visit.statement.value])visit.entry=visit.statement;
        loop_entry_occurrences_[visit.statement.value].root=visit.entry;
        canonical_scopes_[visit.statement.value]=visit.scope;
        // LoopBody prints this owned continuing occurrence as a native
        // statement, outside its condition expression. Restore that lifetime
        // before applying its own nested projections; no arm/layout matching.
        if (retained_bodies_[visit.statement.value] == visit.projector && visit.projector) {
          visit.projector.reset(); visit.projected = false;
        }
        visit.entered = true; visit.projected |= projected_[visit.statement.value] != 0;
        if (!visit.projector) visit.projector = projectors_[visit.statement.value];
        // A prospective ForDeclaration owns test/body/update as one lexical
        // lifetime. Entry observations resolve to this owner after traversal;
        // outside/catch/finally reads remain outside its subtree interval.
        if (std::holds_alternative<BlockStmt>(node) || loops_.initializers[visit.statement.value]) {
          container_parents_[visit.statement.value] = visit.lexical;
          container_order_[next_container_] = visit.statement;
          containers_[visit.statement.value].begin = next_container_++;
          visit.lexical = visit.statement;
          // Index canonical scope at the same lexical census boundary as projected
          // regions. Later passes consume this fact without rescanning syntax.
          if(const auto* block=std::get_if<BlockStmt>(&node)) {
            source_scope_regions_[block->scope.value]=SourceRegionId{visit.statement.value};
            source_allocations_[visit.statement.value]=block->scope;
          } else source_allocations_[visit.statement.value]=LegacyLoopAllocation{};
        }
        lexical_[visit.statement.value] = visit.lexical;
        statement_owners_[visit.statement.value]=visit.projector?visit.projector:visit.lexical;
        Enter(visit);
      }
      const auto child = StatementChild(node, visit.child++, [&](const BlockStmt& block) {
        return std::span<const StmtId>(tree_.children).subspan(block.first_child, block.child_count);
      });
      if (!child) {
        if (std::holds_alternative<BlockStmt>(node) || loops_.initializers[visit.statement.value])
          containers_[visit.statement.value].end = next_container_;
        --pending; continue;
      }
      const auto container = std::holds_alternative<BlockStmt>(node) ? std::optional{visit.statement} : std::nullopt;
      const auto projected = visit.projected; const auto lexical = visit.lexical; const auto projector = visit.projector;
      const auto scope=visit.scope;
      statements_[pending++] = {*child, container, lexical, projector, 0, projected};
      statements_[pending-1].scope=scope;
      statements_[pending-1].entry=visit.entry;
    }
    for(auto& owner:statement_owners_) {budget_.Step();if(owner)owner=lexical_[owner->value];}
    // Syntax recovery owns source region construction once. Publish current
    // occurrence ownership to the shared flow; consumers never infer lifetime
    // from a declaration's old arena position or perform another scope census.
    for(std::size_t i=0;i<tree_.statements.size();++i) {
      budget_.Step();source_regions_[i]={{containers_[i].begin},{containers_[i].end}};
      if(statement_owners_[i])source_occurrences_[i]=SourcePosition{containers_[statement_owners_[i]->value].begin};
      if(loop_entry_occurrences_[i].root)if(const auto region=source_scope_regions_[canonical_scopes_[i].value])
        loop_entry_occurrences_[i].enclosing=SourcePosition{containers_[region->value].begin};
    }
    for(std::size_t i=0;i<uses_.size();++i){budget_.Step();original_declarations_[i]=uses_[i].declaration;}
    for(std::size_t i=0;i<expression_owners_.size();++i) {
      budget_.Step();if(expression_projections_[i]&&expression_owners_[i])expression_owners_[i]=lexical_[expression_owners_[i]->value];
    }
    // Native expression programs execute in their existing projection owner's
    // scope (for headers/updates or optional expressions), not the canonical
    // body block. Resolve after traversal: initializer owners can follow roots.
    // Outermost projection ownership propagates through all descendants.
    for (std::size_t i = 0; i < projection_count_; ++i) {
      budget_.Step(); const auto& observation = projection_observations_[i];
      ObserveContainer(uses_[observation.symbol.value], lexical_[observation.owner.value]);
    }
    // Shared spelling activation publishes nearest-shadow facts once. These
    // canonical names match final reference resolution; projected lifetimes
    // and initialization remain separate current-revision facts.
    auto spelling=AnalyzeTreeSpellings(tree_,budget_);
    unique_spellings_=std::move(spelling.unique);
    spellings_=std::move(spelling.bindings);name_spellings_=std::move(spelling.names);
    const auto& shadowed=spelling.shadowed;
    budget_.Array(source_shadows_,shadowed.size());std::size_t shadow_count=0;
    for(std::size_t i=0;i<shadowed.size();++i) {
      budget_.Step();if(shadowed[i])source_shadows_[shadow_count++]={*shadowed[i],source_scope_regions_[tree_.symbols[i].scope.value],SymbolId{static_cast<std::uint32_t>(i)}};
    }
    source_shadows_.resize(shadow_count);
    // Source entry identity is adapted once after capture/signature census.
    // Mapped arguments and unmodeled signature observations cannot ground a
    // stable value. Retained body writes are checked by the common value solver.
    mapped_arguments_=cells_.mapped_arguments;
    budget_.Array(stable_entries_,uses_.size());
    for(std::size_t i=0;i<uses_.size();++i){budget_.Step();const auto kind=tree_.symbols[i].kind;
      stable_entries_[i]=!uses_[i].escaped&&!uses_[i].outside_body&&
          ((kind==BindingKind::kParameter&&!mapped_arguments_)||kind==BindingKind::kFunctionSelf);}
    // Normalize redundant writes before dead-definition analysis chooses the
    // retained producer; both passes preserve the original RHS evaluation.
    RunPass(Pass::kStoredResults);
    // Remove discarded inert observations before declaration availability is
    // proved on retained references. Observable reads and RHS effects remain.
    RunPass(Pass::kInertOperands);
    RunPass(Pass::kInertStatements);
    // Remove dead value definitions before propagating their retained values.
    // Declaration placement waits for the normalized read/producer projection;
    // an early grammar decision would retain observations later made literal.
    Reach(RecoveryBarrier::kDemand);
    // Literal stability also depends on retained writes. Clear identity stores
    // before coverage, then clear new identities exposed by literal projection.
    RunPass(Pass::kStoredResults);
    RunPass(Pass::kInertOperands);
    RunPass(Pass::kInertStatements);
    RunPass(Pass::kConstants);
    RunPass(Pass::kStoredResults);
    RunPass(Pass::kInertOperands);
    RunPass(Pass::kInertStatements);
    // Value substitution may retire reads. Normalize stores/effects, then
    // discharge independent definition/initialization/binding obligations
    // before aliases consume retained producer coverage. The checkpoint is
    // revision-owned; no producer-specific change booleans control consumers.
    // Place initialization after value normalization, before producer coverage.
    // Existing dominance/extent proofs consume the current retained projection.
    RunPass(Pass::kDeclarations);
    RunPass(Pass::kAliases);
    RunPass(Pass::kStoredResults);
    RunPass(Pass::kInertOperands);
    RunPass(Pass::kInertStatements);
    Reach(RecoveryBarrier::kStorage);
    RunPass(Pass::kSnapshots);
    RunPass(Pass::kStorage);
    RunPass(Pass::kStoredResults);
    RunPass(Pass::kInertOperands);
    RunPass(Pass::kInertStatements);
    // Final aliases consume the coalesced slots. No storage pass is legal
    // after entering grammar, where loop/declaration spelling consumes them.
    RunPass(Pass::kAliases);
    RunPass(Pass::kStoredResults);
    RunPass(Pass::kInertOperands);
    RunPass(Pass::kInertStatements);
    Reach(RecoveryBarrier::kSnapshotRefresh);
    // Storage/alias cleanup can remove names or observations that blocked the
    // earlier snapshot attempt. Reconsider once at this stable stage boundary,
    // with fresh versioned facts. No whole-function retry loop or round cap.
    if(schedule_.SnapshotRefreshNeeded()) {
      RunPass(Pass::kSnapshots);
      RunPass(Pass::kStoredResults);
      RunPass(Pass::kInertOperands);
      RunPass(Pass::kInertStatements);
    }
    // Stable binding values consume final storage/snapshot read destinations.
    // Doing this before motion can choose a name unavailable at that destination.
    Reach(RecoveryBarrier::kReadValues);
    RunPass(Pass::kBindingValues);
    // Preserve entry effects after final read destinations are fixed; residual
    // control normalization cannot reopen storage, alias or snapshot choices.
    RunPass(Pass::kStatementValues);
    Reach(RecoveryBarrier::kDemand);
    RunPass(Pass::kInertOperands);
    // Read substitution exposes inert controls only after private writes and
    // binding lifetimes have been discharged by their independent owners.
    RunPass(Pass::kInertStatements);
    // Final source grammar consumes the final bindings/evaluations. It can
    // retain outlined prefix effects without reviving removed declarations.
    Reach(RecoveryBarrier::kGrammar);
    RunPass(Pass::kLoopDeclarations);
    RunPass(Pass::kDeclarationKinds);
    for (std::size_t i = 0; i < loops_.initializer_roots.size(); ++i) {
      budget_.Step(); plan_.entry_counts[i + 1] = plan_.entry_counts[i] +
          !plan_.omitted[loops_.initializer_roots[i].statement.value];
    }
    CurrentFlow();
    AnalyzeExpressions();
    plan_.normal_stores = std::move(CurrentFacts().projection->expressions.normal_stores);
    plan_.evaluations = std::move(CurrentFacts().projection->expressions.evaluations);
    return Finish();
  }
 private:
  using Pass=RecoveryPass;
  using ExpressionFacts=RecoveryExpressions;
  // All event-indexed and expression facts belong to one immutable projection.
  // A commit destroys this aggregate, so adding an analysis cannot accidentally
  // omit its invalidation. Occurrence initialization is discarded too;
  // destroyed cache admission is released, while the peak/work stay counted.
  struct ProjectionFacts {
    ProjectionRevision revision;
    std::optional<FlowInitializations> initializations;
    std::optional<RecoveryProjection> projection;
    std::optional<FlowLiveness> liveness;
    std::optional<FlowDemand> demand;
    std::optional<FlowConstants> constants;
    std::optional<FlowCopies> copies;
    std::optional<FlowStatements> statements;
    std::optional<FlowCycles> cycles;
    std::size_t admitted_bytes=0;
  };
  std::span<const std::uint8_t> InitializedReads() const {
    return CurrentProjection().initialized_reads;
  }
  ProjectionFacts& CurrentFacts() const {
    RequireAnalysis();
    if(!facts_)facts_.emplace(ProjectionFacts{schedule_.Revision(),{},{},{},{},{},{},{},{}});
    schedule_.RequireCurrent(facts_->revision);
    return *facts_;
  }
  // The first write enters a one-way commit phase. Analysis cannot observe a
  // partly applied batch, and scheduling derives changes from the revision,
  // never a hand-maintained boolean returned by an individual pass.
  void RetainFacts(RecoveryFactSet required) {
    RequireAnalysis();if(!facts_)return;
    schedule_.RequireCurrent(facts_->revision);
    const auto retain=[&](RecoveryFact kind,auto& product) {
      if(required.Contains(kind)||!product)return;
      const auto bytes=product->allocated_bytes;
      RequireTree(bytes<=facts_->admitted_bytes,TreeStructureError::kOwnership);
      product.reset();facts_->admitted_bytes-=bytes;budget_.Release(bytes);
    };
    // A pass boundary has no outstanding consumer borrows. Keep the coupled
    // flow/expression bootstrap for reuse, but release derived products outside
    // the next pass's closed dependency contract. This is cache ownership, not
    // a projection change, proof refresh, resource-based admission or new round.
    retain(RecoveryFact::kInitializations,facts_->initializations);
    retain(RecoveryFact::kLiveness,facts_->liveness);
    retain(RecoveryFact::kDemand,facts_->demand);
    retain(RecoveryFact::kConstants,facts_->constants);
    retain(RecoveryFact::kCopies,facts_->copies);
    retain(RecoveryFact::kStatements,facts_->statements);
    retain(RecoveryFact::kCycles,facts_->cycles);
  }
  bool RunPass(Pass pass) {
    while(const auto prerequisite=schedule_.Prerequisite(pass))RunPassOnce(*prerequisite);
    return RunPassOnce(pass);
  }
  void Reach(RecoveryBarrier barrier) {
    budget_.Step();
    while(const auto prerequisite=schedule_.Prerequisite(barrier))RunPassOnce(*prerequisite);
    schedule_.Arrive(barrier);
  }
  bool RunPassOnce(Pass pass) {
    budget_.Step();schedule_.Start(pass);
    RetainFacts(RequiredFacts(pass));
    if(schedule_.AlreadyStable())return schedule_.Finish();
    switch(pass) {
      case Pass::kDeclarations:Declarations();break;
      case Pass::kDeclarationKinds:DeclarationKinds();break;
      case Pass::kConstants:Constants();break;
      case Pass::kBindingValues:BindingValues();break;
      case Pass::kAliases:Aliases();break;
      case Pass::kSnapshots:ForwardSnapshots();break;
      case Pass::kLoopDeclarations:ProjectLoopDeclarations();break;
      case Pass::kStoredResults:ElideStoredResults();break;
      case Pass::kDeadDefinitions:ElideDeadDefinitions();break;
      case Pass::kInitialValues:ElideInitialValues();break;
      case Pass::kBindings:ElideUnusedBindings();break;
      case Pass::kStatementValues:ProjectStatementValues();break;
      case Pass::kInertStatements:OmitInertStatements();break;
      case Pass::kInertOperands:ElideInertOperands();break;
      case Pass::kStorage:Coalesce();break;
    }
    return schedule_.Finish();
  }
  void BeginCommit() {
    if(!schedule_.Commit())return;
    const auto bytes=facts_?facts_->admitted_bytes:0;
    facts_.reset();
    budget_.Release(bytes);
  }
  void RequireAnalysis() const {schedule_.RequireAnalysis();}
  struct DeclarationCandidate {
    DeclarationSite site;
    ExprId assignment;
    FlowPoint completion;
  };
  std::vector<std::optional<DeclarationCandidate>> DeclarationCandidates() const {
    std::vector<std::optional<DeclarationCandidate>> candidates;
    budget_.Array(candidates,uses_.size());
    bool candidate=false;
    for(const auto& use:uses_) {
      budget_.Step();candidate |= use.declaration && !use.escaped;
    }
    if(!candidate)return candidates;
    const auto& flow=CurrentFlow();const auto& expressions=AnalyzeExpressions();
    auto analyzed=AnalyzeFlowDominance(flow,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    const auto& order=std::get<FlowDominance>(analyzed);
    budget_.Admit(order.allocated_bytes,1);budget_.Step(order.work);
    for(const auto& definition:flow.definitions) {
      budget_.Step();const auto& use=uses_[definition.binding.value];
      if(!use.declaration || use.escaped || !definition.write ||
          flow.bindings[definition.binding.value].initialization==FlowDefinitionKind::kInitializer ||
          !expressions.normal_stores[definition.write->value])continue;
      // Declaration spelling needs an owned whole-statement assignment (or a
      // supported native for declarator), not a write nested in another use.
      // This is the printer's declaration extent, not a CFG layout premise.
      const auto discarded=flow.events[definition.point.value].discarded;
      if(!discarded)continue;
      const auto statement=flow.discarded[discarded->value].statement;
      const auto owner=declaration_containers_[statement.value];
      const auto* effect=std::get_if<ExpressionStmt>(&tree_.statements[statement.value].statement);
      if(!owner || !effect || expressions.roots[effect->expression.value]!=*definition.write)continue;
      auto& selected=candidates[definition.binding.value];
      const auto position=order.tree.nodes[definition.point.value].begin;
      const auto previous=selected?order.tree.nodes[selected->completion.value].begin:0;
      // An ancestor precedes its descendants in dominator preorder. A valid
      // initializer must dominate every write, including other candidates;
      // therefore only the earliest candidate can satisfy all references.
      // Prefer reachable candidates; unreachable syntax has no order demand.
      if(!selected || (position && (!previous || position<previous)))
        selected=DeclarationCandidate{{statement,*owner},*definition.write,definition.point};
    }
    for(const auto& reference:flow.references) {
      budget_.Step();auto& selected=candidates[reference.binding.value];
      if(selected && order.tree.nodes[reference.point.value].begin &&
          !order.Dominates(selected->completion,reference.point))selected.reset();
    }
    return candidates;
  }
  void Declarations() {
    RequireAnalysis();
    const auto candidates=DeclarationCandidates();
    const auto extents=RetainedExtents();
    std::vector<DeclarationEdit> proposed;
    budget_.Array(proposed,uses_.size());std::size_t count=0;
    for (std::size_t i = 0; i < uses_.size(); ++i) {
      budget_.Step(); const auto& use = uses_[i];
      if(!use.declaration)continue;
      std::optional<DeclarationSite> site;std::optional<ExprId> assignment;
      // This grammar action replaces a later assignment with initialization;
      // an existing lexical initializer already owns that binding completion.
      if(CurrentFlow().bindings[i].initialization==FlowDefinitionKind::kInitializer)continue;
      if(use.escaped) {
        // Transitional capture proof: child-cell observations are not fully
        // represented in shared flow. Preserve the original admission and
        // cell activation; never narrow an escaping cell into a new loop cell.
        if(!use.legacy_initializer || use.module_alias)continue;
        const auto statement=*use.legacy_initializer;
        const auto container=declaration_containers_[statement.value];
        const auto& effect=std::get<ExpressionStmt>(tree_.statements[statement.value].statement);
        if(!container || use.first!=effect.expression)continue;
        const auto* owner=std::get_if<BlockStmt>(&tree_.statements[container->value].statement);
        if(!owner || owner->scope!=tree_.symbols[i].scope)continue;
        site=DeclarationSite{statement,*container};assignment=effect.expression;
      } else if(const auto& selected=candidates[i]) {
        site=selected->site;assignment=selected->assignment;
      }
      if(!site || (use.escaped ? !LegacyConfined(use,site->container) :
          !Confined(extents[i],site->container)))continue;
      // Completion and lexical extent are independent obligations. Retain a
      // mutable binding here; final declaration spelling consumes later writes.
      proposed[count++] = {*site,
          {SymbolId{static_cast<std::uint32_t>(i)}, *assignment, DeclarationKind::kLet}};
    }
    // Grammar and allocation are part of the immutable declaration proposal.
    // A prefix cannot declare a loop cell. Decide this before publishing any
    // placement: later copy/effect removal must not promote an earlier prefix
    // into a different environment. The final grammar pass consumes the same
    // provider; it does not acquire authority to relocate a committed binding.
    std::vector<std::uint8_t> prefix;
    if(count && !loops_.initializer_roots.empty()) {
      schedule_.RequireFact(RecoveryFact::kLoopEntries);
      const auto before=budget_.ActiveBytes();
      [&] {
        std::vector<std::optional<BindingInitializer>> trial;
        budget_.Array(trial,plan_.statements.size());
        std::copy(plan_.statements.begin(),plan_.statements.end(),trial.begin());
        for(std::size_t i=0;i<count;++i){budget_.Step();trial[proposed[i].site.statement.value]=proposed[i].initializer;}
        auto candidate=BindingPlanView{plan_};candidate.statements=trial;
        auto grammar=PlanLoopDeclarations(loops_,candidate,original_declarations_,budget_.Remaining());
        if(const auto* error=std::get_if<TreeStructureError>(&grammar))throw *error;
        const auto& entries=std::get<LoopDeclarations>(grammar);
        budget_.Admit(entries.allocated_bytes,1);budget_.Step(entries.work);
        budget_.Array(prefix,plan_.statements.size());
        for(const auto& entry:entries.entries){budget_.Step();if(!entry.loop)prefix[entry.statement.value]=1;}
      }();
      budget_.Release(budget_.ActiveBytes()-before-prefix.capacity()*sizeof(std::uint8_t));
    }
    std::size_t accepted=0;
    for(std::size_t i=0;i<count;++i) {
      budget_.Step();if(prefix.empty() || !prefix[proposed[i].site.statement.value])proposed[accepted++]=proposed[i];
    }
    if(!accepted)return;
    // All placements are one source proposal. Rebuild grammar, allocations,
    // initialization and lookup together before changing any committed facet.
    // A rejected whole batch retains the original representation; dropping
    // members of a validated namespace would require another full proof.
    const auto edits=std::span<const DeclarationEdit>{proposed}.first(accepted);
    if(!CheckDeclarationProposal(edits))return;
    for(const auto& edit:edits) {
      budget_.Step();BeginCommit();
      declaration_sites_[edit.initializer.symbol.value]=edit.site;
      plan_.statements[edit.site.statement.value]=edit.initializer;
      DiscardDeclaration(*uses_[edit.initializer.symbol.value].declaration);
    }
  }
  bool CheckDeclarationProposal(std::span<const DeclarationEdit> edits) const {
    const auto& current=CurrentFlow();
    const auto before=budget_.ActiveBytes();
    const bool accepted=[&] {
      std::vector<std::optional<BindingInitializer>> statements;
      std::vector<std::optional<DeclarationSite>> sites;
      std::vector<std::uint8_t> omitted,moved;
      std::vector<std::optional<DeclarationValue>> values;
      std::vector<std::optional<DeclarationCompletion>> completions;
      budget_.Array(statements,plan_.statements.size());budget_.Array(sites,declaration_sites_.size());
      budget_.Array(omitted,plan_.omitted.size());budget_.Array(values,plan_.declaration_values.size());
      budget_.Array(completions,plan_.declaration_completions.size());budget_.Array(moved,uses_.size());
      std::copy(plan_.statements.begin(),plan_.statements.end(),statements.begin());
      std::copy(declaration_sites_.begin(),declaration_sites_.end(),sites.begin());
      std::copy(plan_.omitted.begin(),plan_.omitted.end(),omitted.begin());
      std::copy(plan_.declaration_values.begin(),plan_.declaration_values.end(),values.begin());
      std::copy(plan_.declaration_completions.begin(),plan_.declaration_completions.end(),completions.begin());
      for(const auto& edit:edits) {
        budget_.Step();const auto binding=edit.initializer.symbol;
        statements[edit.site.statement.value]=edit.initializer;sites[binding.value]=edit.site;
        RetireDeclaration(*uses_[binding.value].declaration,omitted,values,completions);
        // The existing captured-cell prerequisite remains transitional: this
        // ordinary-unit proof does not certify closure cell reconstruction.
        if(!uses_[binding.value].escaped)moved[plan_.Storage(binding).value]=1;
      }
      auto view=BindingPlanView{plan_};view.statements=statements;view.omitted=omitted;
      view.declaration_values=values;view.declaration_completions=completions;
      const auto trial=BuildProjectedFlow(view,sites);
      // Trial publication already solved lexical reads together with value,
      // effect and completion dependencies. Never reconstruct a second proof
      // here; statement and expression reads share the event certificate.
      for(const auto& reference:trial.flow.references) {
        budget_.Step();
        if(moved[reference.binding.value] && reference.kind==FlowReferenceKind::kRead &&
            trial.flow.events[reference.point.value].read_completion!=FlowReadCompletion::kInitialized)return false;
      }
      std::vector<SourceNameQuery> queries;
      RequireTree(trial.flow.references.size()<=std::numeric_limits<std::size_t>::max()-trial.flow.names.size(),TreeStructureError::kResourceLimit);
      budget_.Array(queries,trial.flow.references.size()+trial.flow.names.size());std::size_t at=0;
      for(const auto& reference:trial.flow.references){budget_.Step();queries[at++]={reference.binding,reference.owner};}
      for(const auto& name:trial.flow.names){budget_.Step();queries[at++]={name.name,name.owner};}
      const auto original_names=ResolveNames(current,queries);
      const auto trial_names=ResolveNames(trial.flow,queries);
      for(std::size_t i=0;i<queries.size();++i) {
        budget_.Step();
        const auto* binding=std::get_if<SymbolId>(&queries[i].name);
        if((binding&&moved[binding->value]) || original_names[i]==SourceNameResolution::kResolved)
          if(trial_names[i]!=SourceNameResolution::kResolved)return false;
      }
      return true;
    }();
    // The synchronous trial and every borrowed view have been destroyed.
    // Release scratch admission, retaining cumulative work and peak bytes.
    budget_.Release(budget_.ActiveBytes()-before);return accepted;
  }
  void DeclarationKinds() {
    const auto eligible=[&](std::size_t statement) {
      if(!plan_.statements[statement] || plan_.omitted[statement])return false;
      if(plan_.statements[statement]->declaration==DeclarationKind::kConst)return false;
      // Native for declarators share one grammar-level let keyword. Leave that
      // group to LoopDeclarations; this pass finalizes standalone declarations.
      if(loops_.initializer_owners[statement])return false;
      const auto root=plan_.Storage(plan_.statements[statement]->symbol);
      return uses_[root.value].private_storage && !uses_[root.value].escaped;
    };
    bool candidate=false;
    for(std::size_t i=0;i<plan_.statements.size();++i){budget_.Step();candidate |= eligible(i);}
    if(!candidate)return;
    const auto& flow=CurrentFlow();const auto& expressions=AnalyzeExpressions();
    std::vector<std::size_t> writes;std::vector<std::optional<ExprId>> initializers;
    budget_.Array(writes,uses_.size());budget_.Array(initializers,uses_.size());
    for(const auto& reference:flow.references) {
      budget_.Step();if(reference.kind==FlowReferenceKind::kWrite)++writes[reference.binding.value];
    }
    for(const auto& definition:flow.definitions) {
      budget_.Step();if(definition.kind==FlowDefinitionKind::kInitializer)
        initializers[definition.binding.value]=definition.write;
    }
    std::vector<StmtId> proposed;budget_.Array(proposed,plan_.statements.size());std::size_t count=0;
    for(std::size_t i=0;i<plan_.statements.size();++i) {
      budget_.Step();if(!eligible(i))continue;
      const auto& declaration=*plan_.statements[i];const auto root=plan_.Storage(declaration.symbol);
      if(writes[root.value]==1 && initializers[root.value]==expressions.roots[declaration.assignment.value])
        proposed[count++]={static_cast<std::uint32_t>(i)};
    }
    // This changes only declaration spelling after the storage/lifetime plan
    // is final. No initializer, write, scope or evaluation is removed here.
    for(std::size_t i=0;i<count;++i) {
      budget_.Step();BeginCommit();plan_.statements[proposed[i].value]->declaration=DeclarationKind::kConst;
    }
  }
  LoopDeclarations LoopDeclarationsFor(BindingPlanView candidate) const {
    RequireAnalysis();
    schedule_.RequireFact(RecoveryFact::kLoopEntries);
    const auto planned=PlanLoopDeclarations(loops_,candidate,original_declarations_,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&planned))throw *error;
    auto delta=std::get<LoopDeclarations>(std::move(planned));
    budget_.Admit(delta.allocated_bytes,1);budget_.Step(delta.work);
    return delta;
  }
  void ProjectLoopDeclarations() {
    RequireAnalysis();
    if(loops_.initializer_roots.empty())return;
    const auto delta=LoopDeclarationsFor(plan_);
    if(delta.starts.empty() && delta.demoted.empty() && delta.declarators.empty())return;
    BeginCommit();
    for(const auto& start:delta.starts){budget_.Step();plan_.loop_declarations[start.loop.value]=start.first;}
    for(const auto root:delta.demoted) {
      budget_.Step();plan_.statements[root.value].reset();
      if(auto& completion=plan_.declaration_completions[root.value])completion->kind=DeclarationCompletionKind::kAssignUndefined;
    }
    for(const auto& declarator:delta.declarators) {
      budget_.Step();plan_.statements[declarator.initializer.value]->declaration=DeclarationKind::kLet;
      DiscardDeclaration(declarator.declaration);
    }
  }
  void ElideInertOperands() {
    // Comma discards its left value. Shared completion/effect facts, including
    // TDZ, coercion and suspension, decide elision before downstream liveness.
    // A logical operand is discarded only when the joint control domain proves
    // it cannot execute. Consume canonical equations and shared domains; never
    // guess from a literal/sequence shape. Keep all left evaluation and value.
    // Replacement preserves GetValue and suppresses NamedEvaluation. Printer
    // consumes this decision and only projects the removed origin ranges.
    std::size_t capacity=0;
    for(std::size_t i=0;i<tree_.expressions.size();++i) {
      budget_.Step();if(!plan_.replacements[i] &&
          (std::holds_alternative<SequenceExpr>(tree_.expressions[i].expression)||
           std::holds_alternative<FlowLogicalValue>(value_equations_[i])))++capacity;
    }
    if(!capacity)return;
    const auto& facts=AnalyzeExpressions();
    const auto& flow=CurrentFlow();
    struct OperandElision { ExprId expression,value; };
    std::vector<OperandElision> proposed;budget_.Array(proposed,capacity);std::size_t count=0;
    const RecoveryExpressionInput input{plan_,value_equations_,{}};
    for(std::size_t i=0;i<tree_.expressions.size();++i) {
      budget_.Step();if(plan_.replacements[i])continue;
      const ExprId id{static_cast<std::uint32_t>(i)};
      const auto equation=ProjectedRecoveryEquation(input,id);
      if(const auto* logical=std::get_if<FlowLogicalValue>(&equation)) {
        RequireTree(logical->left.value<flow.normal_truths.size(),TreeStructureError::kOwnership);
        const auto left=flow.normal_truths[logical->left.value];
        if(!left.Empty()&&(left&LogicalRightDomains(logical->operation)).Empty())proposed[count++]={id,logical->left};
        continue;
      }
      const auto* sequence=std::get_if<SequenceExpr>(&tree_.expressions[i].expression);
      if(!sequence)continue;
      if(facts.evaluations[sequence->left.value]==ExpressionEvaluation::kInert)
        proposed[count++]={id,sequence->right};
      else if(facts.evaluations[sequence->right.value]==ExpressionEvaluation::kInert) {
        if(flow.value_uses[i]==FlowValueUse::kDiscarded)proposed[count++]={id,sequence->left};
      }
    }
    for(std::size_t i=0;i<count;++i) {
      budget_.Step();const auto& edit=proposed[i];BeginCommit();
      plan_.replacements[edit.expression.value]=edit.value;
    }
  }
  void ProjectStatementValues() {
    const auto& facts=CurrentStatements();const auto& flow=CurrentFlow();
    struct Proposal {StmtId statement;ExprId value;};
    std::vector<Proposal> proposed;budget_.Array(proposed,flow.statement_order.size());std::size_t count=0;
    for(const auto id:flow.statement_order) {
      budget_.Step();const auto& statement=*flow.statement_semantics[id.value];
      if(statement.entry_value&&!plan_.statement_values[id.value]&&facts.residuals[id.value].Inert())
        proposed[count++]={id,*statement.entry_value};
    }
    for(std::size_t i=0;i<count;++i){budget_.Step();const auto& edit=proposed[i];BeginCommit();plan_.statement_values[edit.statement.value]=edit.value;}
  }
  void OmitInertStatements() {
    const auto omitted = PlanInertStatements(CurrentStatements());
    // The pass publishes a complete proposal before changing the projection.
    // No subsequent statement observes a partly applied omission batch.
    for (const auto statement : omitted) {
      budget_.Step(); BeginCommit();plan_.omitted[statement.value] = 1;
    }
  }
  std::size_t ProjectionCapacity() {
    RequireTree(cells_.captures.size()<=std::numeric_limits<std::size_t>::max()-tree_.expressions.size(),TreeStructureError::kResourceLimit);
    return tree_.expressions.size()+cells_.captures.size();
  }
  void ForwardSnapshots() {
    const auto eligible=[&](std::size_t i) {
      const auto& use=uses_[i];
      return use.private_storage && use.declaration &&
          !use.escaped && !plan_.aliases[i] && !plan_.literals[i];
    };
    bool candidate=false;
    for(std::size_t i=0;i<uses_.size();++i){budget_.Step();candidate |= eligible(i);}
    if(!candidate)return;
    const auto& flow=CurrentFlow();
    const auto covered=AnalyzeFlowReadCoverage(flow,CurrentLiveness(),budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&covered))throw *error;
    const auto& coverage=std::get<FlowReadCoverage>(covered);
    budget_.Admit(coverage.allocated_bytes,1);budget_.Step(coverage.work);
    std::vector<const FlowDefinition*> definitions;
    budget_.Array(definitions,uses_.size());
    for(std::size_t i=0;i<flow.definitions.size();++i) {
      budget_.Step();const auto& definition=flow.definitions[i];
      if(coverage.definitions[i] && definition.value && (definition.write || definition.declaration) &&
          (definition.kind==FlowDefinitionKind::kAssignment||definition.kind==FlowDefinitionKind::kInitializer))
        definitions[definition.binding.value]=&definition;
    }
    std::vector<const FlowReference*> reads,stores;
    std::vector<std::size_t> counts, write_counts;
    std::vector<std::uint8_t> bound;
    std::vector<std::optional<StmtId>> scope_containers, environments;
    budget_.Array(reads, uses_.size()); budget_.Array(counts, uses_.size());
    budget_.Array(stores,uses_.size());budget_.Array(write_counts,uses_.size());
    for(const auto& reference:flow.references) {
      budget_.Step();switch(reference.kind) {
        case FlowReferenceKind::kRead:
          ++counts[reference.binding.value];reads[reference.binding.value]=&reference;break;
        case FlowReferenceKind::kWrite:
          ++write_counts[reference.binding.value];
          stores[reference.binding.value]=&reference;
          break;
      }
    }
    // Must execute the same next observation on every completion path. The
    // shared solver accounts for effects, state, exits and transparent cycles;
    // original statement counts/linear epochs no longer decide motion.
    std::optional<FlowMotion> motion;
    const auto pairing=[&]() -> const FlowMotion& {
      RequireAnalysis();
      // A refresh may have no single-definition/read candidate after storage
      // sharing. Build the common motion proof lazily, once per unchanged pass.
      if(!motion) {
        auto analyzed=AnalyzeFlowMotion(flow,CurrentLiveness(),budget_.Remaining());
        if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
        motion=std::get<FlowMotion>(std::move(analyzed));
        budget_.Admit(motion->allocated_bytes,1);budget_.Step(motion->work);
      }
      return *motion;
    };
    std::vector<SnapshotMove> proposed;
    std::vector<StmtId> source_owners;budget_.Array(source_owners,uses_.size());
    budget_.Array(proposed,uses_.size());std::size_t count=0;
    for (std::size_t i = 0; i < uses_.size(); ++i) {
      budget_.Step();
      // Count retained writes in the same projection as read coverage and
      // motion. Original Scan counts include stores eliminated by earlier
      // passes; they cannot decide single-definition snapshot recovery.
      if(!eligible(i) || counts[i]!=1 || write_counts[i]!=1 || !definitions[i])continue;
      const auto& definition=*definitions[i];
      // Removing PutValue must preserve its own completion, not just the
      // moved RHS. Shared write effects prove initialized writable storage;
      // lexical initialization is a separate, non-PutValue definition kind.
      if(definition.kind==FlowDefinitionKind::kAssignment && definition.effect!=FlowWriteEffect::kInert)continue;
      const auto read=std::get<ExprId>(reads[i]->site);
      if(!stores[i])continue;
      const auto owner=definition.write?stores[i]->owner:definition.declaration;
      if(!owner)continue;
      const auto statement=*owner;
      // Source occurrence removal must preserve its enclosing value. The
      // common adapter, not whole-statement syntax, proves a discarded result.
      // A lexical initializer has no separately observed assignment result.
      if(definition.write && definition.kind==FlowDefinitionKind::kAssignment &&
          flow.value_uses[definition.write->value]!=FlowValueUse::kDiscarded)continue;
      const auto& paired=pairing();
      if(paired.next[stores[i]->point.value]!=reads[i]->point ||
          paired.previous[reads[i]->point.value]!=stores[i]->point)continue;
      // Bidirectional observation pairing proves execution count. The shared
      // handler/cleanup identity separately preserves throws and abrupt resume.
      if(!stores[i]->exceptions || stores[i]->exceptions!=reads[i]->exceptions)continue;
      const auto old_owner=ReferenceOwner(*stores[i]),new_owner=ReferenceOwner(*reads[i]);
      if(!old_owner || !new_owner)continue;
      const auto rhs=*definition.value;
      // Native naming follows the projected destination. Moving a direct
      // creation can require a computed-name wrapper; retain strict resource
      // contracts until no additional allocation is proved for that destination.
      if (ResourceFailures(tree_.identity.contract) != ResourceFailureScope::kExcluded &&
          (std::holds_alternative<FunctionExpr>(tree_.expressions[rhs.value].expression) ||
           std::holds_alternative<ClassExpr>(tree_.expressions[rhs.value].expression))) continue;
      source_owners[count]=*old_owner;
      proposed[count++]={SymbolId{static_cast<std::uint32_t>(i)},*uses_[i].declaration,read,rhs,definition.write,statement,
          flow.bindings[i].declaration};
    }
    if(!count)return;
    // Environment signatures are consumed only by actual motion proposals.
    // A failed/no-candidate refresh does not allocate or rebuild this index.
    budget_.Array(bound, tree_.statements.size());
    budget_.Array(scope_containers, tree_.scopes.size());
    budget_.Array(environments, tree_.statements.size());
    for (std::size_t i = 0; i < next_container_; ++i) {
      budget_.Step(); const auto id = container_order_[i];
      if (const auto* block = std::get_if<BlockStmt>(&tree_.statements[id.value].statement))
        scope_containers[block->scope.value] = id;
    }
    // Include both original lexical bindings and prospective native declarations.
    // Empty scopes have the same environment signature. This proves all names,
    // including free names of created closures, without a subtree walk per copy.
    for (std::size_t i = 0; i < tree_.symbols.size(); ++i) {
      budget_.Step(); const auto& use = uses_[i];
      if (use.declaration && (plan_.omitted[use.declaration->value] ||
          plan_.declaration_values[use.declaration->value])) continue;
      if (const auto owner = scope_containers[tree_.symbols[i].scope.value]) bound[owner->value] = 1;
    }
    for (std::size_t i = 0; i < plan_.statements.size(); ++i) {
      budget_.Step(); if (!plan_.statements[i]) continue;
      const auto& site=declaration_sites_[plan_.statements[i]->symbol.value];
      if(site)bound[site->container.value]=1;
    }
    for (std::size_t i = 0; i < next_container_; ++i) {
      budget_.Step(); const auto id = container_order_[i]; const auto parent = container_parents_[id.value];
      environments[id.value] = bound[id.value] || !parent ? id : environments[parent->value];
    }
    proposed.resize(count);
    std::vector<SnapshotSelection> selections;budget_.Array(selections,count);
    std::fill(selections.begin(),selections.end(),SnapshotSelection::kForward);
    const auto prepare=[&]() {
      auto planned=PlanSnapshotProjection(plan_,proposed,selections,undefined_value_,budget_.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&planned))throw *error;
      auto delta=std::get<SnapshotProjection>(std::move(planned));
      budget_.Admit(delta.allocated_bytes,1);budget_.Step(delta.work);return delta;
    };
    // Full motion proposal -> indexed restoration closure -> actual transaction.
    // These are separate proof phases, not a per-binding graph retry loop.
    // Destroy each temporary graph before crediting its admission or building
    // the next one. The current flow remains immutable throughout the pass.
    const auto scratch=budget_.ActiveBytes();
    {
      const auto delta=prepare();
      const auto trial=BuildProjectedFlow(delta.View(plan_),declaration_sites_,delta.transported_reads);
      const auto& projected=trial.flow;
      std::vector<SourceRetentionDemand> demands;budget_.Array(demands,count);
      std::size_t demand_count=0;
      const auto retain=[&](SymbolId binding){demands[demand_count++]={binding,{}};};
      for(std::size_t i=0;i<count;++i) {
        budget_.Step();const auto& action=proposed[i];const auto root=projected.expression_transports[action.value.value];
        if(!root){retain(action.binding);continue;}
        const auto owner=projected.expression_owners[action.value.value];
        if(!owner||!statement_owners_[owner->value]){retain(*root);continue;}
        const auto environment=statement_owners_[owner->value];
        if(environments[source_owners[i].value]!=environments[environment->value]&&
            (ResourceFailures(tree_.identity.contract)==ResourceFailureScope::kObservable||
             trial.expressions.lexical[action.value.value]==LexicalDependency::kEnclosingEnvironment))retain(*root);
      }
      demands.resize(demand_count);
      auto analyzed=PlanSnapshotRetention(flow,projected,proposed,demands,budget_.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
      auto retirement=std::get<SnapshotRetention>(std::move(analyzed));
      budget_.Admit(retirement.allocated_bytes,1);budget_.Step(retirement.work);
      std::copy(retirement.selections.begin(),retirement.selections.end(),selections.begin());
    }
    budget_.Release(budget_.ActiveBytes()-scratch);
    if(std::none_of(selections.begin(),selections.end(),[](auto selection){return selection==SnapshotSelection::kForward;}))return;
    auto final_delta=prepare();
    const auto final_scratch=budget_.ActiveBytes();
    {
      const auto trial=BuildProjectedFlow(final_delta.View(plan_),declaration_sites_,final_delta.transported_reads);
      std::vector<SourceNameQuery> queries;
      RequireTree(trial.flow.references.size()<=std::numeric_limits<std::size_t>::max()-trial.flow.names.size(),TreeStructureError::kResourceLimit);
      budget_.Array(queries,trial.flow.references.size()+trial.flow.names.size());std::size_t size=0;
      for(const auto& reference:trial.flow.references) {
        budget_.Step();const auto* expression=std::get_if<ExprId>(&reference.site);if(!expression)continue;
        if(trial.flow.expression_transports[expression->value])queries[size++]={reference.binding,reference.owner};
      }
      for(const auto& name:trial.flow.names) {
        budget_.Step();if(trial.flow.expression_transports[name.site.value])queries[size++]={name.name,name.owner};
      }
      queries.resize(size);const auto resolved=ResolveNames(trial.flow,queries);
      // The indexed closure certifies this exact selected projection. A proof
      // inconsistency is a typed model error, never an optimistic subset commit.
      for(const auto resolution:resolved){budget_.Step();RequireTree(resolution==SourceNameResolution::kResolved,TreeStructureError::kOwnership);}
    }
    budget_.Release(budget_.ActiveBytes()-final_scratch);
    const auto bytes=[]<class T>(const std::vector<T>& values){return values.capacity()*sizeof(T);};
    const auto old_bytes=bytes(plan_.statements)+bytes(plan_.omitted)+bytes(plan_.declaration_values)+
        bytes(plan_.declaration_completions)+bytes(plan_.replacements)+bytes(plan_.forwarded_values)+bytes(plan_.transports);
    const auto read_bytes=bytes(final_delta.transported_reads);
    BeginCommit();std::move(final_delta).Publish(plan_);
    budget_.Release(old_bytes);
    std::vector<std::optional<SymbolId>>{}.swap(final_delta.transported_reads);budget_.Release(read_bytes);
  }
  std::vector<SourceNameResolution> ResolveNames(const RecoveryFlow& flow,std::span<const SourceNameQuery> queries) const {
    RequireAnalysis();auto analyzed=ResolveSourceNames(flow,queries,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    auto result=std::get<SourceNameResults>(std::move(analyzed));
    budget_.Admit(result.allocated_bytes,1);budget_.Step(result.work);return std::move(result.resolutions);
  }
  RecoveryProjection BuildProjectedFlow(BindingPlanView candidate,
      std::span<const std::optional<DeclarationSite>> sites,
      std::span<const std::optional<SymbolId>> transported_reads={}) const {
    RequireAnalysis();
    RequireTree(sites.size()==uses_.size(),TreeStructureError::kOwnership);
    std::vector<std::uint8_t> private_bindings;
    std::vector<SourceBindingScope> source_bindings;
    std::vector<SourceBindingScope> original_bindings;
    budget_.Array(private_bindings,uses_.size());
    budget_.Array(source_bindings,uses_.size());
    budget_.Array(original_bindings,uses_.size());
    for (std::size_t i=0;i<uses_.size();++i) {
      budget_.Step();private_bindings[i]=tree_.symbols[i].kind==BindingKind::kCompilerTemporary && !uses_[i].escaped;
      const auto scope=tree_.symbols[i].scope;const auto region=source_scope_regions_[scope.value];
      if(region)source_bindings[i]=OriginalBindingScope{region};
      else switch(tree_.scopes[scope.value].kind) {
        case ScopeKind::kParameters:case ScopeKind::kFunctionSelf:
        case ScopeKind::kCaptures:case ScopeKind::kModule:source_bindings[i]=EnclosingBindingScope{};break;
        case ScopeKind::kFunctionBody:case ScopeKind::kBlock:case ScopeKind::kCatch:
          source_bindings[i]=UnmodeledBindingScope{};break;
        default:throw TreeStructureError::kScope;
      }
      original_bindings[i]=source_bindings[i];
      const auto& site=sites[i];
      if(site&&candidate.statements[site->statement.value])source_bindings[i]=ProjectedBindingScope{SourceRegionId{site->container.value}};
    }
    LegacyFlowProjection projection{candidate.aliases,candidate.literals,candidate.replacements,
        candidate.omitted,{},{},candidate.statements,private_bindings,
        candidate.storage,candidate.undefined_stores,candidate.declaration_values,candidate.declaration_completions,
        {source_regions_,source_occurrences_,source_bindings,source_shadows_,unique_spellings_,spellings_,name_spellings_,source_allocations_},candidate.read_literals,candidate.transports,transported_reads,candidate.read_bindings,stable_entries_,{},candidate.read_sources,candidate.statement_values,{},original_};
    projection.loops=&loops_;projection.cells=&cells_;
    std::optional<LoopDeclarations> loop_declarations;
    std::optional<LoopEntryProjection> loop_entries;
    std::size_t loop_bytes=0;
    if(!loops_.initializer_roots.empty()) {
      const auto before=budget_.ActiveBytes();
      loop_declarations=LoopDeclarationsFor(candidate);
      if(!loop_declarations->entries.empty()) {
        auto adapted=ProjectLoopEntryFlow(*loop_declarations,candidate.statements,candidate.declaration_completions,
            original_bindings,projection.source_scopes,loop_entry_occurrences_,budget_.Remaining());
        if(const auto* error=std::get_if<TreeStructureError>(&adapted))throw *error;
        loop_entries=std::get<LoopEntryProjection>(std::move(adapted));
        budget_.Admit(loop_entries->allocated_bytes,1);budget_.Step(loop_entries->work);
        candidate.statements=loop_entries->initializers;
        candidate.declaration_completions=loop_entries->completions;
        projection.source_scopes.bindings=loop_entries->bindings;
        projection.source_scopes.occurrences=loop_entries->occurrences;
        projection.source_scopes.loop_allocations=loop_entries->allocations;
        projection.source_scopes.loop_owners=loop_entries->owners;
      }
      loop_bytes=budget_.ActiveBytes()-before;
    }
    const RecoveryExpressionInput input{candidate,value_equations_,
        std::span<const ExprId>(completions_).first(completion_count_),original_};
    auto prepared=PrepareRecoveryProjection(tree_,projection,input,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&prepared))throw *error;
    auto result=std::get<RecoveryProjection>(std::move(prepared));
    budget_.Admit(result.allocated_bytes,1);budget_.Step(result.work);
    // The prepared graph owns all facts it publishes. No borrowed projection
    // span survives; release temporary ownership only after construction.
    loop_entries.reset();loop_declarations.reset();budget_.Release(loop_bytes);return result;
  }
  const RecoveryProjection& CurrentProjection() const {
    schedule_.RequireFact(RecoveryFact::kFlow);
    auto& facts=CurrentFacts();
    if(!facts.projection) {
      // Publish only the completed joint product. Publishing conservative
      // expressions first would let a later flow query change the precision
      // seen by an in-progress pass without advancing its revision.
      const auto before=budget_.ActiveBytes();
      auto prepared=BuildProjectedFlow(plan_,declaration_sites_);
      facts.admitted_bytes+=budget_.ActiveBytes()-before;
      facts.projection.emplace(std::move(prepared));
    }
    return *facts.projection;
  }
  const RecoveryFlow& CurrentFlow() const {return CurrentProjection().flow;}
  const FlowCycles& CurrentCycles() const {
    schedule_.RequireFact(RecoveryFact::kCycles);auto& facts=CurrentFacts();
    if(!facts.cycles) {
      auto analyzed=AnalyzeFlowCycles(CurrentFlow(),budget_.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
      facts.cycles=std::get<FlowCycles>(std::move(analyzed));
      budget_.Admit(facts.cycles->allocated_bytes,1);budget_.Step(facts.cycles->work);
      facts.admitted_bytes+=facts.cycles->allocated_bytes;
    }
    return *facts.cycles;
  }
  const FlowStatements& CurrentStatements() const {
    schedule_.RequireFact(RecoveryFact::kStatements);auto& facts=CurrentFacts();
    if(!facts.statements) {
      const auto& flow=CurrentFlow();bool needed=false;
      for(const auto& statement:flow.statement_semantics){budget_.Step();needed|=statement&&statement->repeat_point.has_value();}
      const FlowCycles absent;const auto& cycles=needed?CurrentCycles():absent;
      auto analyzed=AnalyzeFlowStatements(flow,cycles,budget_.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
      facts.statements=std::get<FlowStatements>(std::move(analyzed));
      budget_.Admit(facts.statements->allocated_bytes,1);budget_.Step(facts.statements->work);
      facts.admitted_bytes+=facts.statements->allocated_bytes;
    }
    return *facts.statements;
  }
  const FlowLiveness& CurrentLiveness() const {
    schedule_.RequireFact(RecoveryFact::kLiveness);
    auto& facts=CurrentFacts();
    if(facts.liveness)return *facts.liveness;
    const auto& flow=CurrentFlow();
    auto analyzed=AnalyzeFlowLiveness(flow,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    facts.liveness=std::get<FlowLiveness>(std::move(analyzed));
    budget_.Admit(facts.liveness->allocated_bytes,1);budget_.Step(facts.liveness->work);
    facts.admitted_bytes+=facts.liveness->allocated_bytes;return *facts.liveness;
  }
  const FlowDemand& CurrentDemand() const {
    schedule_.RequireFact(RecoveryFact::kDemand);
    auto& facts=CurrentFacts();
    if(!facts.demand) {
      auto analyzed=AnalyzeFlowDemand(CurrentFlow(),budget_.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
      facts.demand=std::get<FlowDemand>(std::move(analyzed));
      budget_.Admit(facts.demand->allocated_bytes,1);budget_.Step(facts.demand->work);
      facts.admitted_bytes+=facts.demand->allocated_bytes;
    }
    return *facts.demand;
  }
  const FlowInitializations& CurrentInitializations() const {
    schedule_.RequireFact(RecoveryFact::kInitializations);
    auto& facts=CurrentFacts();
    const auto& flow=CurrentFlow();
    // Storage placement needs declaration anchors, final-graph dominance and
    // additional point queries. This common analysis does not replace the
    // read certificates already published by CurrentProjection.
    if(!facts.initializations) {
      auto analyzed=AnalyzeFlowInitializations(flow,budget_.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
      facts.initializations=std::get<FlowInitializations>(std::move(analyzed));
      budget_.Admit(facts.initializations->allocated_bytes,1);budget_.Step(facts.initializations->work);
      facts.admitted_bytes+=facts.initializations->allocated_bytes;
    }
    return *facts.initializations;
  }
  bool HasPrivateBindings() const {
    bool candidate=false;
    for(std::size_t i=0;i<uses_.size();++i) {
      budget_.Step();candidate |= tree_.symbols[i].kind==BindingKind::kCompilerTemporary && !uses_[i].escaped;
    }
    return candidate;
  }
  void ElideDeadDefinitions() {
    // Source naming recovery preserves NamedEvaluation after an assignment is
    // projected to its RHS, but can require a naming envelope. Its allocation
    // equivalence is not proved under the legacy resource-observable contract.
    // Retain that contract's representation without inspecting producer shapes.
    if (ResourceFailures(tree_.identity.contract)!=ResourceFailureScope::kExcluded) return;
    if(!HasPrivateBindings())return;
    const auto& flow=CurrentFlow();
    const auto& demand=CurrentDemand();
    auto proposed=PlanDefinitionElisions(flow,demand,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&proposed))throw *error;
    const auto& delta=std::get<DefinitionElisions>(proposed);
    budget_.Admit(delta.allocated_bytes,1);budget_.Step(delta.work);
    // Removing a dead value store retains its RHS and lexical completion.
    // Binding removal runs separately on a fresh projection after this commit.
    if(delta.statements.empty() && delta.writes.empty())return;
    BeginCommit();
    for (const auto statement:delta.statements) {budget_.Step();plan_.omitted[statement.value]=1;}
    for(const auto& write:delta.writes) {
      budget_.Step();std::visit([&](auto site) {
        using T=decltype(site);
        if constexpr(std::is_same_v<T,ExprId>) {
          RequireTree(write.value.has_value(),TreeStructureError::kOwnership);
          plan_.replacements[site.value]=*write.value;
        } else {
          if(write.value)plan_.declaration_values[site.value]=DeclarationValue{*write.value,{}};
          else plan_.omitted[site.value]=1;
          plan_.undefined_stores[site.value].reset();plan_.statements[site.value].reset();
          plan_.declaration_completions[site.value].reset();
        }
      },write.site);
    }
  }
  void DiscardDeclaration(StmtId statement) {
    RetireDeclaration(statement,plan_.omitted,plan_.declaration_values,plan_.declaration_completions);
  }
  void ElideInitialValues() {
    if(ResourceFailures(tree_.identity.contract)!=ResourceFailureScope::kExcluded || !HasPrivateBindings())return;
    auto proposed=FindDeadInitialValues(CurrentFlow(),CurrentDemand(),budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&proposed))throw *error;
    const auto& delta=std::get<InitialValueElisions>(proposed);
    budget_.Admit(delta.allocated_bytes,1);budget_.Step(delta.work);
    if(delta.values.empty())return;
    BeginCommit();
    for(const auto& value:delta.values) {
      budget_.Step();const auto& native=plan_.statements[value.statement.value];
      const auto declaration=native?native->declaration:tree_.symbols[value.binding.value].declaration;
      plan_.declaration_completions[value.statement.value]=DeclarationCompletion{value.binding,value.evaluation,declaration};
    }
  }
  void ElideUnusedBindings() {
    // The same naming/resource contract as dead-value projection applies.
    if(ResourceFailures(tree_.identity.contract)!=ResourceFailureScope::kExcluded || !HasPrivateBindings())return;
    const auto& flow=CurrentFlow();
    auto proposed=FindUnusedBindings(flow,CurrentDemand(),budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&proposed))throw *error;
    const auto& delta=std::get<BindingElisions>(proposed);
    budget_.Admit(delta.allocated_bytes,1);budget_.Step(delta.work);
    if(delta.declarations.empty())return;
    // The proposal owns its IDs. No facts are consulted after BeginCommit.
    // Required RHS GetValue stays at the original occurrence and handler;
    // no remaining syntax refers to a removed binding, including TDZ reads.
    BeginCommit();
    for(const auto& action:delta.declarations) {
      budget_.Step();
      if(!action.value)plan_.omitted[action.statement.value]=1;
      else if(action.write)plan_.replacements[action.write->value]=*action.value;
      else plan_.declaration_values[action.statement.value]=DeclarationValue{*action.value,{}};
      plan_.statements[action.statement.value].reset();
      plan_.declaration_completions[action.statement.value].reset();
    }
  }
  void Coalesce() {
    const auto eligible = [&](SymbolId symbol) {
      const auto& use = uses_[symbol.value];
      return use.private_storage && use.declaration && MutableBinding(symbol) && !use.escaped && !use.outside_body &&
          !plan_.aliases[symbol.value] && !plan_.literals[symbol.value] &&
          (ResourceFailures(tree_.identity.contract) == ResourceFailureScope::kExcluded || !use.named_creation);
    };
    bool candidate = false;
    for (const auto& expression : tree_.expressions) {
      budget_.Step(); const auto* assignment = std::get_if<AssignmentExpr>(&expression.expression);
      if (!assignment) continue;
      const auto* target = std::get_if<SymbolTarget>(&assignment->target);
      // The RHS may be a forwarded copy chain. Affinity comes from the common
      // projected expression model, never from its original local syntax.
      candidate |= target && eligible(target->symbol);
    }
    for(const auto& statement:tree_.statements) {
      budget_.Step();if(const auto* declaration=std::get_if<DeclarationStmt>(&statement.statement))
        candidate |= declaration->initializer && eligible(declaration->symbol);
    }
    if (!candidate) return;
    const auto& flow=CurrentFlow();
    const auto& liveness=CurrentLiveness();
    auto analyzed = FindBindingInterference(flow,liveness,budget_.Remaining());
    if (const auto* error = std::get_if<TreeStructureError>(&analyzed)) throw *error;
    auto facts = std::get<BindingInterference>(std::move(analyzed));
    budget_.Admit(facts.allocated_bytes, 1); budget_.Step(facts.work);
    const auto& initialization_order=CurrentInitializations();
    std::vector<StorageBinding> bindings;budget_.Array(bindings,uses_.size());
    for(std::size_t i=0;i<bindings.size();++i) {
      budget_.Step();const SymbolId symbol{static_cast<std::uint32_t>(i)};
      const auto& site=declaration_sites_[i];
      bindings[i].eligible=eligible(symbol);
      bindings[i].unique_spelling=flow.bindings[i].unique_spelling;
      bindings[i].native_declaration=site&&plan_.statements[site->statement.value].has_value();
      if(const auto region=flow.BindingRegion(symbol))
        bindings[i].binding_region=flow.source_regions[region->value];
      if(const auto point=initialization_order.declarations[i]) {
        const auto node=initialization_order.order.tree.nodes[point->value];
        if(node.begin) {
          bindings[i].initialization_order=node.begin;
          bindings[i].initialized_region=CompletionRegion{node.begin,node.end};
        }
      }
    }
    // Representative choice consumes the same actual locations and completion
    // dominance as the later lifetime/initialization proofs. Aggregate each
    // coordinate domain once; do not walk a graph per slot or mix the domains.
    std::vector<StorageReference> name_references;budget_.Array(name_references,flow.source_shadows.empty()?0:flow.references.size());
    std::size_t located=0;
    std::vector<StorageShadow> shadows;budget_.Array(shadows,flow.source_shadows.size());
    for(std::size_t i=0;i<shadows.size();++i) {
      budget_.Step();const auto& shadow=flow.source_shadows[i];
      shadows[i]={shadow.binding,shadow.region?std::optional{flow.source_regions[shadow.region->value]}:std::nullopt};
    }
    for(const auto& reference:flow.references) {
      budget_.Step();auto& binding=bindings[reference.binding.value];
      const auto completion=initialization_order.order.tree.nodes[reference.point.value].begin;
      if(completion) {
        const CompletionRegion observed{completion,completion+1};
        if(binding.reachable_references)binding.reachable_references=CompletionRegion{
            std::min(binding.reachable_references->first,observed.first),std::max(binding.reachable_references->last,observed.last)};
        else binding.reachable_references=observed;
      }
      const auto position=reference.owner?flow.source_occurrences[reference.owner->value]:std::nullopt;
      if(!position){binding.unlocated_reference=true;continue;}
      if(!flow.source_shadows.empty())name_references[located++]={reference.binding,*position};
      RequireTree(position->value<std::numeric_limits<std::size_t>::max(),TreeStructureError::kResourceLimit);
      const SourceRegion region{*position,{position->value+1}};
      if(binding.reference_region)binding.reference_region=SourceRegion{
          std::min(binding.reference_region->first,region.first),std::max(binding.reference_region->last,region.last)};
      else binding.reference_region=region;
    }
    auto coalesced=PlanStorageCoalescing(facts,bindings,budget_.Remaining(),{shadows,std::span<const StorageReference>{name_references}.first(located)});
    if(const auto* error=std::get_if<TreeStructureError>(&coalesced))throw *error;
    auto delta=std::get<StorageCoalescing>(std::move(coalesced));
    budget_.Admit(delta.allocated_bytes,1);budget_.Step(delta.work);
    auto proposed=std::move(delta.storage);
    bool sharing=false;for(const auto root:proposed){budget_.Step();sharing|=root.has_value();}
    if(!sharing)return;
    // Candidate storage asks additional states of the SAME immutable flow.
    // Query demand is distinct from a plan mutation or manual stale refresh.
    std::vector<LiveBinding> initialization_queries;
    RequireTree(flow.references.size()<=std::numeric_limits<std::size_t>::max()-flow.definitions.size(),TreeStructureError::kResourceLimit);
    const auto occurrences=flow.references.size()+flow.definitions.size();
    budget_.Array(initialization_queries,occurrences);std::size_t queried=0;
    for(const auto& reference:flow.references){budget_.Step();if(const auto root=proposed[reference.binding.value])initialization_queries[queried++]={reference.point,*root};}
    for(const auto& definition:flow.definitions){budget_.Step();if(const auto root=proposed[definition.binding.value])initialization_queries[queried++]={definition.point,*root};}
    auto candidate_initialized=AnalyzeFlowInitializations(flow,std::span<const LiveBinding>{initialization_queries}.first(queried),budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&candidate_initialized))throw *error;
    const auto& candidate_initialization=std::get<FlowInitializations>(candidate_initialized);
    budget_.Admit(candidate_initialization.allocated_bytes,1);budget_.Step(candidate_initialization.work);
    // Initialization preservation is member-local with fixed representatives.
    // Its owned actions remain valid if name/lifetime closure retains a subset;
    // there is no graph or plan mutation between these independent analyses.
    auto initialized=PlanStorageInitializations(flow,liveness,candidate_initialization,proposed,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&initialized))throw *error;
    const auto& initialization=std::get<StorageInitializations>(initialized);
    budget_.Admit(initialization.allocated_bytes,1);budget_.Step(initialization.work);
    std::vector<SourceRetirement> candidates_mask;
    std::vector<SourceRetirementQuery> references;
    std::vector<SourceRetentionDemand> demands;
    RequireTree(occurrences<=std::numeric_limits<std::size_t>::max()-proposed.size(),TreeStructureError::kResourceLimit);
    budget_.Array(candidates_mask,proposed.size());budget_.Array(references,occurrences);budget_.Array(demands,occurrences+proposed.size());
    std::size_t count=0,demand_count=0;
    for(std::size_t i=0;i<proposed.size();++i) {
      budget_.Step();if(!proposed[i])continue;candidates_mask[i]=SourceRetirement::kProposed;
      if(initialization.blocked[i])demands[demand_count++]={SymbolId{static_cast<std::uint32_t>(i)},{}};
    }
    // All retained reads/writes survive storage sharing, including forwarded
    // RHSs and update halves. Their current semantic owners, not original arena
    // order, define lookup. Storage removal never makes an RHS read conditional.
    for(const auto& reference:flow.references) {
      budget_.Step();const auto root=proposed[reference.binding.value];if(!root)continue;
      RequireTree(reference.owner.has_value(),TreeStructureError::kOwnership);
      references[count++]={{*root,reference.owner},reference.binding,{}};
      // New initialized storage cannot mask an original pre-initialization
      // access. Completion and lexical lifetime stay independent of spelling.
      if(!candidate_initialization.Completed(*root,reference.point)||
          !candidate_initialization.Completed(reference.binding,reference.point)||
          !flow.BindingInScope(*root,reference.owner))demands[demand_count++]={reference.binding,{}};
    }
    // A live undefined initialization becomes a write to the retained slot.
    // Check its actual declaration occurrence in the same final namespace;
    // keeping its old binding removes that proposed write without changing any
    // other member's independently proved initialization action.
    for(const auto& definition:flow.definitions) {
      budget_.Step();const auto binding=definition.binding;const auto root=proposed[binding.value];
      if(definition.kind!=FlowDefinitionKind::kUndefined||!definition.declaration||!root||!initialization.stores[definition.declaration->value])continue;
      const auto statement=*definition.declaration;references[count++]={{*root,statement},binding,{}};
      if(!flow.BindingInScope(*root,statement))demands[demand_count++]={binding,{}};
    }
    references.resize(count);demands.resize(demand_count);
    auto names=AnalyzeSourceNameRetirement(flow,candidates_mask,references,demands,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&names))throw *error;
    auto retirement=std::get<SourceNameRetirement>(std::move(names));
    budget_.Admit(retirement.allocated_bytes,1);budget_.Step(retirement.work);
    // Completion occurrences include recovered declarations, not just original
    // placeholders. Own every remap before commit; shared initialization/liveness
    // decides whether its undefined value needs a successful slot write.
    struct StorageCompletion {StmtId statement;std::optional<SymbolId> write;};
    std::vector<StorageCompletion> completions;
    budget_.Array(completions,plan_.declaration_completions.size());std::size_t completion_count=0;
    for(std::size_t i=0;i<plan_.declaration_completions.size();++i) {
      budget_.Step();const auto& completion=plan_.declaration_completions[i];
      if(!completion || completion->kind!=DeclarationCompletionKind::kInitialize ||
          !proposed[completion->binding.value] || retirement.presence[completion->binding.value]==SourceBindingPresence::kRetained)continue;
      completions[completion_count++]={StmtId{static_cast<std::uint32_t>(i)},initialization.stores[i]};
    }
    completions.resize(completion_count);
    for (std::size_t i = 0; i < proposed.size(); ++i) {
      budget_.Step(); if (!proposed[i] || retirement.presence[i]==SourceBindingPresence::kRetained) continue;
      const auto root = *proposed[i]; const auto& use = uses_[i];
      BeginCommit();plan_.storage[i] = root;
      // Lifetime removal and value preservation are separate plan actions.
      if(!plan_.declaration_completions[use.declaration->value]) {
        if(initialization.stores[use.declaration->value])plan_.undefined_stores[use.declaration->value]=root;
        else if(const auto value=std::get<DeclarationStmt>(tree_.statements[use.declaration->value].statement).initializer)
          plan_.declaration_values[use.declaration->value]=DeclarationValue{*value,root};
        else plan_.omitted[use.declaration->value]=1;
      }
      if(const auto& site=declaration_sites_[i])plan_.statements[site->statement.value].reset();
      if(const auto& site=declaration_sites_[root.value];site && plan_.statements[site->statement.value])
        plan_.statements[site->statement.value]->declaration=DeclarationKind::kLet;
    }
    for(const auto& completion:completions) {
      budget_.Step();BeginCommit();
      if(completion.write) {
        auto& retained=*plan_.declaration_completions[completion.statement.value];
        retained.binding=*completion.write;retained.kind=DeclarationCompletionKind::kAssignUndefined;
      }else DiscardDeclaration(completion.statement);
    }
  }
  bool MutableBinding(SymbolId symbol) const {return WritableRecoveryBinding(tree_,symbol);}
  void ElideStoredResults() {
    // Analysis and proposals observe one immutable projection. Only this
    // commit changes replacements; consumers derive their own current facts.
    const auto& facts = AnalyzeExpressions();
    const auto replacements = PlanStoredResultElisions(facts);
    std::vector<StmtId> declaration_values;
    budget_.Array(declaration_values,plan_.declaration_values.size());std::size_t count=0;
    for(std::size_t i=0;i<plan_.declaration_values.size();++i) {
      budget_.Step();const auto& value=plan_.declaration_values[i];
      if(value && value->target && facts.bindings[value->value.value]==plan_.Storage(*value->target))
        declaration_values[count++]={static_cast<std::uint32_t>(i)};
    }
    ApplyStoredResultElisions(replacements);
    for(std::size_t i=0;i<count;++i) {
      budget_.Step();BeginCommit();plan_.declaration_values[declaration_values[i].value]->target.reset();
    }
  }
  const ExpressionFacts& AnalyzeExpressions() const {
    schedule_.RequireFact(RecoveryFact::kExpressions);
    return CurrentProjection().expressions;
  }
  void IndexLiteralTexts() {
    // Intern equal UTF-16 values once, independent of projection revisions.
    // Budget character comparisons; never rescan long text at every join.
    budget_.Array(literal_texts_,tree_.texts.size());
    std::vector<TextId> order;budget_.Array(order,tree_.texts.size());
    for(std::size_t i=0;i<order.size();++i){budget_.Step();order[i]={static_cast<std::uint32_t>(i)};}
    const auto compare=[&](TextId left,TextId right) {
      budget_.Step();const auto& a=tree_.texts[left.value];const auto& b=tree_.texts[right.value];
      for(std::size_t i=0;i<std::min(a.size(),b.size());++i) {
        budget_.Step();if(a[i]!=b[i])return a[i]<=>b[i];
      }
      return a.size()<=>b.size();
    };
    std::sort(order.begin(),order.end(),[&](TextId a,TextId b){return compare(a,b)<0;});
    std::optional<TextId> previous;
    for(const auto id:order) {
      budget_.Step();literal_texts_[id.value]=previous&&compare(*previous,id)==0?literal_texts_[previous->value]:id;
      previous=id;
    }
  }
  void IndexPrimitiveValues() {
    budget_.Array(expression_primitives_,tree_.expressions.size());
    std::size_t count=1;  // ECMA-262 empty lexical declaration initializes undefined.
    for(const auto& expression:tree_.expressions){budget_.Step();count+=std::holds_alternative<TreeLiteral>(expression.expression);}
    RequireTree(count<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    budget_.Array(plan_.primitives,count);
    std::uint32_t next=0;undefined_value_={next++};plan_.primitives[undefined_value_.value]=UndefinedLiteral{};
    for(std::size_t i=0;i<tree_.expressions.size();++i) {
      budget_.Step();if(const auto* literal=std::get_if<TreeLiteral>(&tree_.expressions[i].expression)) {
        const PrimitiveId id{next++};expression_primitives_[i]=id;auto value=*literal;
        if(auto* text=std::get_if<TextId>(&value))*text=literal_texts_[text->value];
        plan_.primitives[id.value]=value;
      }
    }
  }
  void IndexValueEquations() {
    // Immutable source instruction meaning, adapted once. Projection below
    // transports committed replacements/storage/value identities; it never
    // re-infers language meaning from a rewritten syntax shape.
    budget_.Array(value_equations_,tree_.expressions.size());
    for(std::size_t i=0;i<tree_.expressions.size();++i) {
      budget_.Step();const auto& node=tree_.expressions[i].expression;auto& equation=value_equations_[i];
      if(std::holds_alternative<TreeLiteral>(node))equation=FlowPrimitiveReference{*expression_primitives_[i]};
      else if(const auto* read=std::get_if<SymbolExpr>(&node))equation=FlowBindingValue{read->symbol};
      else if(const auto* assignment=std::get_if<AssignmentExpr>(&node))equation=FlowExpressionValue{assignment->value};
      else if(const auto* sequence=std::get_if<SequenceExpr>(&node))equation=FlowExpressionValue{sequence->right};
      else if(const auto* choice=std::get_if<ConditionalExpr>(&node))equation=FlowChoiceValue{choice->condition,choice->consequent,choice->alternate};
      else if(const auto* logical=std::get_if<LogicalExpr>(&node))equation=FlowLogicalValue{logical->operation,logical->left,logical->right};
    }
  }
  FlowValueEquation ProjectedBindingValue(SymbolId id) const {
    RequireAnalysis();
    if(const auto primitive=plan_.literals[id.value])return FlowPrimitiveReference{*primitive};
    return FlowBindingValue{plan_.ReadBinding(id)};
  }
  const TreeLiteral& LiteralValue(PrimitiveId literal) const {return plan_.primitives[literal.value];}
  bool SameLiteral(PrimitiveId left,PrimitiveId right) const {return SamePrimitiveValue(LiteralValue(left),LiteralValue(right));}
  std::vector<std::optional<ExprId>> PlanStoredResultElisions(const ExpressionFacts& facts) const {
    std::vector<std::optional<ExprId>> replacements;
    budget_.Array(replacements, tree_.expressions.size());
    for (std::size_t at = 0; at < completion_count_; ++at) {
      budget_.Step(); const auto id = completions_[at];
      if (plan_.replacements[id.value]||plan_.transports[id.value]) continue;
      const auto* assignment = std::get_if<AssignmentExpr>(&tree_.expressions[id.value].expression);
      // The result-binding fact is produced only for writable declarative
      // assignment targets. Missing identity is not evidence of equality.
      if (assignment && facts.bindings[id.value] &&
          facts.bindings[id.value] == facts.bindings[assignment->value.value])
        replacements[id.value] = assignment->value;
    }
    return replacements;
  }
  void ApplyStoredResultElisions(const std::vector<std::optional<ExprId>>& replacements) {
    // A proposed outer elision uses the RHS normal-result identity, which is
    // preserved by every inner elision. Thus the batch does not depend on
    // proposal application order; no newly rewritten syntax is re-matched.
    for (std::size_t i = 0; i < replacements.size(); ++i) {
      budget_.Step();
      if (replacements[i]) {
        BeginCommit();plan_.replacements[i] = replacements[i];
      }
    }
  }
  std::vector<StmtId> PlanInertStatements(const FlowStatements& facts) const {
    std::vector<StmtId> omitted;budget_.Array(omitted,tree_.statements.size());std::size_t count=0;
    const auto& flow=CurrentFlow();
    for(const auto id:flow.statement_order) {
      budget_.Step();
      // Grammar retention is distinct from observable computation. All source
      // positions consume the shared role; no AST/root-name exception here.
      if(flow.statement_semantics[id.value]->retention==FlowStatementRetention::kOptional&&facts.observations[id.value].Inert())omitted[count++]=id;
    }
    omitted.resize(count);return omitted;
  }
  bool LegacyConfined(const BindingUses& use, std::optional<StmtId> owner) const {
    if (use.outside_body || !owner || !use.first_container) return false;
    const auto range = containers_[owner->value];
    return *use.first_container >= range.begin && *use.last_container < range.end;
  }
  std::optional<StmtId> ReferenceOwner(const FlowReference& reference) const {
    return reference.owner?statement_owners_[reference.owner->value]:std::nullopt;
  }
  struct BindingExtent {
    std::optional<std::size_t> first,last;
    bool outside=false;
  };
  std::vector<BindingExtent> RetainedExtents() const {
    RequireAnalysis();
    std::vector<BindingExtent> extents;budget_.Array(extents,uses_.size());
    // Signature observations are not modeled by body flow. Keep that explicit
    // gap; absence from the graph is never proof that a signature use vanished.
    for(std::size_t i=0;i<uses_.size();++i) {
      budget_.Step();extents[i].outside=uses_[i].outside_body;
    }
    // Current owners travel with references through replacements and snapshot
    // motion. Source grammar maps those statement owners to lexical destinations.
    // Inspect all retained references, including unreachable printed syntax:
    // control reachability does not excuse a dangling lexical name.
    for(const auto& reference:CurrentFlow().references) {
      budget_.Step();auto& extent=extents[reference.binding.value];
      const auto owner=ReferenceOwner(reference);
      if(!owner){extent.outside=true;continue;}
      const auto position=containers_[owner->value].begin;
      extent.first=extent.first?std::min(*extent.first,position):position;
      extent.last=extent.last?std::max(*extent.last,position):position;
    }
    return extents;
  }
  bool Confined(const BindingExtent& extent,std::optional<StmtId> owner) const {
    if(extent.outside || !owner || !extent.first)return false;
    const auto range=containers_[owner->value];
    return *extent.first>=range.begin && *extent.last<range.end;
  }
  struct CoveredWrite {
    std::optional<ExprId> expression;
    ExprId value;
    std::optional<StmtId> declaration;
    FlowDefinitionKind kind;
    FlowWriteEffect effect;
    std::optional<StmtId> statement;
    std::optional<StmtId> initialization;
  };
  void RemoveCoveredWrite(const CoveredWrite& selected,StmtId placeholder) {
    BeginCommit();
    if(selected.expression) {
      plan_.replacements[selected.expression->value]=selected.value;
      DiscardDeclaration(placeholder);
    } else {
      RequireTree(selected.declaration.has_value(),TreeStructureError::kOwnership);
      plan_.declaration_values[selected.declaration->value]=DeclarationValue{selected.value,{}};
      if(placeholder!=*selected.declaration)DiscardDeclaration(placeholder);
    }
    // Value propagation retires the current binding completion independently
    // of the selected value producer and its original empty placeholder.
    if(selected.initialization && *selected.initialization!=placeholder &&
        plan_.declaration_completions[selected.initialization->value])DiscardDeclaration(*selected.initialization);
  }
  struct BindingReadCoverage {
    std::vector<std::optional<StmtId>> declarations;
    std::vector<std::optional<CoveredWrite>> covered_writes;
    // Retained semantic writes, including projected undefined stores. The
    // original census still locates declaration candidates, not stability.
    std::vector<std::size_t> writes;
    std::span<const FlowReference> references;
  };
  BindingReadCoverage AnalyzeBindingReadCoverage() const {
    const auto& flow=CurrentFlow();
    const auto& expressions=AnalyzeExpressions();
    const auto& live=CurrentLiveness();
    auto analyzed=AnalyzeFlowReadCoverage(flow,live,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    const auto& coverage=std::get<FlowReadCoverage>(analyzed);
    budget_.Admit(coverage.allocated_bytes,1);budget_.Step(coverage.work);
    BindingReadCoverage result;
    budget_.Array(result.declarations,uses_.size());
    budget_.Array(result.covered_writes,uses_.size());
    for(std::size_t i=0;i<flow.definitions.size();++i) {
      budget_.Step();const auto& definition=flow.definitions[i];
      if(coverage.definitions[i] && definition.value && (definition.write || definition.declaration) &&
          (definition.kind==FlowDefinitionKind::kAssignment||definition.kind==FlowDefinitionKind::kInitializer)) {
        CoveredWrite selected{definition.write,*definition.value,definition.declaration,definition.kind,definition.effect,{},
            flow.bindings[definition.binding.value].declaration};
        if(!definition.write)selected.statement=definition.declaration;
        // Ownership identifies what a whole-statement deletion would remove.
        // Other occurrences still support write-to-RHS projection; this is not
        // an admission condition on their control-flow layout.
        const auto discarded=flow.events[definition.point.value].discarded;
        if(discarded && definition.write) {
          const auto statement=flow.discarded[discarded->value].statement;
          const auto* effect=std::get_if<ExpressionStmt>(&tree_.statements[statement.value].statement);
          if(effect && expressions.roots[effect->expression.value]==*definition.write)selected.statement=statement;
        }
        result.covered_writes[definition.binding.value]=selected;
      }
      // Keep the retained initialization identity supplied by this projection.
      // Original traversal order is not evidence that a declaration still
      // exists, initializes this storage, or covers its current reads.
      if(coverage.definitions[i] && definition.declaration &&
          (definition.kind==FlowDefinitionKind::kInitializer || definition.kind==FlowDefinitionKind::kUndefined))
        result.declarations[definition.binding.value]=definition.declaration;
    }
    budget_.Array(result.writes,uses_.size());
    for(const auto& reference:flow.references) {
      budget_.Step();if(reference.kind==FlowReferenceKind::kWrite)++result.writes[reference.binding.value];
    }
    result.references=flow.references;
    return result;
  }
  const FlowConstants& CurrentConstants() const {
    schedule_.RequireFact(RecoveryFact::kConstants);
    auto& facts=CurrentFacts();
    if(!facts.constants) {
      const auto& flow=CurrentFlow();
      std::vector<FlowValueEquation> bindings;budget_.Array(bindings,tree_.symbols.size());
      for(std::size_t i=0;i<bindings.size();++i){budget_.Step();bindings[i]=ProjectedBindingValue({static_cast<std::uint32_t>(i)});}
      const FlowValueEquations expressions{value_equations_,plan_.replacements,bindings,InitializedReads(),plan_.read_literals,{},plan_.transports,plan_.read_sources};
      std::vector<FlowDefinitionValue> values;budget_.Array(values,flow.definitions.size());
      for(std::size_t i=0;i<values.size();++i) {
        budget_.Step();const auto& definition=flow.definitions[i];
        switch(definition.kind) {
          case FlowDefinitionKind::kUndefined:case FlowDefinitionKind::kUndefinedStore:
            values[i]=FlowPrimitiveValue{LiteralValue(undefined_value_),undefined_value_};break;
          case FlowDefinitionKind::kAssignment:case FlowDefinitionKind::kInitializer:
            if(definition.value)values[i]=FlowExpressionValue{*definition.value};
            break;
          case FlowDefinitionKind::kIterationCopy:
            if(definition.input)values[i]=*definition.input;
            break;
          // Update computes a new numeric value; its stored-result identity
          // is not a transparent self-copy equation. Entry/external values
          // remain unknown until their independent semantics are modeled.
          case FlowDefinitionKind::kUpdate:case FlowDefinitionKind::kRestParameter:case FlowDefinitionKind::kParameter:
          case FlowDefinitionKind::kCatch:case FlowDefinitionKind::kFunctionSelf:break;
        }
      }
      auto analyzed=AnalyzeFlowConstants(flow,CurrentLiveness(),values,expressions,plan_.primitives,tree_.texts,budget_.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
      facts.constants=std::get<FlowConstants>(std::move(analyzed));
      budget_.Admit(facts.constants->allocated_bytes,1);budget_.Step(facts.constants->work);
      facts.admitted_bytes+=facts.constants->allocated_bytes;
    }
    return *facts.constants;
  }
  const FlowCopies& CurrentCopies() const {
    schedule_.RequireFact(RecoveryFact::kCopies);auto& facts=CurrentFacts();
    if(!facts.copies) {
      const auto& flow=CurrentFlow();
      std::vector<FlowValueEquation> bindings;budget_.Array(bindings,tree_.symbols.size());
      for(std::size_t i=0;i<bindings.size();++i){budget_.Step();bindings[i]=ProjectedBindingValue({static_cast<std::uint32_t>(i)});}
      const FlowValueEquations expressions{value_equations_,plan_.replacements,bindings,InitializedReads(),plan_.read_literals,{},plan_.transports,plan_.read_sources};
      auto analyzed=AnalyzeFlowCopies(flow,CurrentLiveness(),expressions,budget_.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
      facts.copies=std::get<FlowCopies>(std::move(analyzed));
      budget_.Admit(facts.copies->allocated_bytes,1);budget_.Step(facts.copies->work);
      facts.admitted_bytes+=facts.copies->allocated_bytes;
    }
    return *facts.copies;
  }
  void Constants() {
    bool candidate=false;
    for(const auto& use:uses_){budget_.Step();candidate|=use.private_storage&&use.declaration&&!use.escaped;}
    if(!candidate)return;
    const auto& values=CurrentConstants();
    std::vector<std::optional<PrimitiveId>> proposed;budget_.Array(proposed,uses_.size());
    for(std::size_t i=0;i<uses_.size();++i) {
      budget_.Step();const auto& use=uses_[i];
      if(use.private_storage&&use.declaration&&!use.escaped&&values.values[i])proposed[i]=values.values[i]->literal;
    }
    std::vector<FlowReadConstant> read_proposals;budget_.Array(read_proposals,values.reads.size());std::size_t read_count=0;
    for(const auto& read:values.reads) {
      budget_.Step();const auto* source=std::get_if<FlowBindingValue>(&value_equations_[read.expression.value]);
      RequireTree(source!=nullptr,TreeStructureError::kOwnership);const auto& use=uses_[source->binding.value];
      if(use.private_storage&&use.declaration&&!use.escaped&&
          (!plan_.read_literals[read.expression.value]||!SameLiteral(*plan_.read_literals[read.expression.value],read.primitive)))
        read_proposals[read_count++]=read;
    }
    // Read substitution only. DeadDefinitions/InitialValues/Bindings decide
    // writes, required evaluations and lexical completion on fresh facts.
    for(std::size_t i=0;i<proposed.size();++i) {
      budget_.Step();if(!proposed[i]||(plan_.literals[i]&&SameLiteral(*plan_.literals[i],*proposed[i])))continue;
      BeginCommit();plan_.literals[i]=proposed[i];
    }
    for(std::size_t i=0;i<read_count;++i){budget_.Step();const auto& edit=read_proposals[i];BeginCommit();plan_.read_literals[edit.expression.value]=edit.primitive;}
  }
  void BindingValues() {
    bool candidate=false;
    for(const auto& use:uses_){budget_.Step();candidate|=use.private_storage&&use.declaration&&!use.escaped&&!use.outside_body;}
    if(!candidate)return;
    const auto& values=CurrentConstants();
    std::vector<std::optional<SymbolId>> bindings;budget_.Array(bindings,uses_.size());
    bool invariant=false;
    for(std::size_t i=0;i<uses_.size();++i){budget_.Step();const auto& use=uses_[i];
      if(use.private_storage&&use.declaration&&!use.escaped&&!use.outside_body&&values.bindings[i]&&
          values.bindings[i]->value!=i&&plan_.read_bindings[i]!=values.bindings[i]) {bindings[i]=values.bindings[i];invariant=true;}}
    const auto& copies=CurrentCopies();
    RequireTree(copies.reads.size()<=std::numeric_limits<std::size_t>::max()-values.binding_reads.size(),TreeStructureError::kResourceLimit);
    std::vector<FlowReadBinding> reads;budget_.Array(reads,values.binding_reads.size()+copies.reads.size());
    std::size_t read_count=0;
    // Both common results are sorted by occurrence. Prefer a proved immutable
    // value when available; current-copy equality never becomes an invariant.
    std::size_t value_at=0,copy_at=0;
    while(value_at<values.binding_reads.size()||copy_at<copies.reads.size()) {
      FlowReadBinding read;
      if(value_at<values.binding_reads.size()&&(copy_at==copies.reads.size()||
          values.binding_reads[value_at].expression.value<=copies.reads[copy_at].expression.value)) {
        read=values.binding_reads[value_at++];
        if(copy_at<copies.reads.size()&&copies.reads[copy_at].expression==read.expression)++copy_at;
      }else {const auto& copy=copies.reads[copy_at++];read={copy.expression,copy.source};}
      budget_.Step();const auto* source=std::get_if<FlowBindingValue>(&value_equations_[read.expression.value]);
      RequireTree(source!=nullptr,TreeStructureError::kOwnership);const auto& use=uses_[source->binding.value];
      if(use.private_storage&&use.declaration&&!use.escaped&&!use.outside_body&&
          plan_.ReadBinding(read.expression,source->binding)!=read.binding)reads[read_count++]=read;
    }
    reads.resize(read_count);
    if(!invariant&&reads.empty())return;
    const auto& flow=CurrentFlow();
    std::vector<std::optional<std::size_t>> occurrence;budget_.Array(occurrence,reads.empty()?0:tree_.expressions.size());
    for(std::size_t i=0;i<reads.size();++i){budget_.Step();occurrence[reads[i].expression.value]=i;}
    std::vector<SourceNameQuery> queries;std::vector<SymbolId> owners;std::vector<std::optional<std::size_t>> read_owners;
    std::vector<std::uint8_t> blocked,read_blocked;
    constexpr std::size_t kQueriesPerRead=2; // independent invariant and occurrence proposals
    RequireTree(flow.references.size()<=std::numeric_limits<std::size_t>::max()/kQueriesPerRead,TreeStructureError::kResourceLimit);
    const auto capacity=flow.references.size()*kQueriesPerRead;
    budget_.Array(queries,capacity);budget_.Array(owners,capacity);budget_.Array(read_owners,capacity);
    budget_.Array(blocked,uses_.size());budget_.Array(read_blocked,reads.size());
    std::size_t query_count=0;
    for(const auto& reference:flow.references){
      budget_.Step();if(reference.kind!=FlowReferenceKind::kRead)continue;
      const auto* expression=std::get_if<ExprId>(&reference.site);
      const auto at=expression&&!occurrence.empty()?occurrence[expression->value]:std::nullopt;
      const auto append=[&](SymbolId target,std::optional<std::size_t> read){
        if(!reference.owner||!flow.BindingInScope(target,reference.owner)){
          if(read)read_blocked[*read]=1;else blocked[reference.binding.value]=1;return;
        }
        queries[query_count]={target,reference.owner};owners[query_count]=reference.binding;read_owners[query_count++]=read;
      };
      if(at)append(reads[*at].binding,at);
      if(const auto target=bindings[reference.binding.value])append(*target,{});
    }
    queries.resize(query_count);owners.resize(query_count);read_owners.resize(query_count);
    const auto resolved=ResolveNames(flow,queries);
    for(std::size_t i=0;i<query_count;++i){budget_.Step();if(resolved[i]!=SourceNameResolution::kResolved){
      if(read_owners[i])read_blocked[*read_owners[i]]=1;else blocked[owners[i].value]=1;
    }}
    // Both proposals belong to the same immutable projection. Occurrence
    // substitution never turns its fact into an all-definition invariant.
    // Keep every write/RHS/completion until independent demand cleanup.
    for(std::size_t i=0;i<bindings.size();++i){budget_.Step();if(bindings[i]&&!blocked[i]){BeginCommit();plan_.read_bindings[i]=bindings[i];}}
    for(std::size_t i=0;i<reads.size();++i){budget_.Step();if(!read_blocked[i]){BeginCommit();plan_.read_sources[reads[i].expression.value]=reads[i].binding;}}
  }
  void Aliases() {
    // A single retained definition must cover all source reads, including each
    // copy RHS; each copy definition must in turn cover its destination reads.
    // A source re-execution reaching a destination read without re-executing
    // its copy would also admit a path bypassing that copy from function entry,
    // contradicting coverage. This is the SSA copy-propagation obligation;
    // name resolution, escaping cells and activation remain separate below.
    bool candidate=false;
    for(std::size_t i=0;i<uses_.size();++i) {
      budget_.Step();const auto& use=uses_[i];
      if(!use.private_storage || !use.declaration || use.escaped || plan_.literals[i])continue;
      candidate=true;
    }
    if(!candidate)return;
    const auto& expressions=AnalyzeExpressions();
    // Every earlier commit invalidates these facts. No-op passes can share
    // immutable flow/liveness; coverage and producer identity use one projection.
    const auto coverage=AnalyzeBindingReadCoverage();
    const auto extents=RetainedExtents();
    const auto& flow=CurrentFlow();
    std::vector<std::optional<SymbolId>> roots;
    std::vector<std::uint8_t> discard_rhs;
    budget_.Array(discard_rhs,uses_.size());
    budget_.Array(roots,uses_.size());std::size_t ready=0;
    // ECMA-262 2024 FunctionDeclarationInstantiation: only a non-strict,
    // non-arrow simple parameter list may couple formals to mapped arguments.
    // Scope activation initializes catch/self and all body-entry parameters;
    // local/module/capture absence of writes is not an initialization proof.
    const auto implicit=[&](BindingKind kind) {
      switch(kind) {
        case BindingKind::kParameter:return !mapped_arguments_;
        case BindingKind::kCatchParameter:case BindingKind::kFunctionSelf:return true;
        case BindingKind::kLocal:case BindingKind::kCompilerTemporary:
        case BindingKind::kCapture:case BindingKind::kModule:return false;
      }
      throw TreeStructureError::kBinding;
    };
    for(std::size_t i=0;i<uses_.size();++i) {
      budget_.Step();constant_heads_[i].reset();constant_next_[i].reset();
    }
    for(std::size_t i=0;i<uses_.size();++i) {
      budget_.Step();const auto& use=uses_[i];
      const auto symbol=SymbolId{static_cast<std::uint32_t>(i)};
      if(!coverage.writes[i]&&!use.escaped&&implicit(tree_.symbols[i].kind)) {
        roots[i]=symbol;constant_queue_[ready++]=symbol;continue;
      }
      // Declaration completion is another initialization event, not an opcode
      // or producer shape. Retain its original value/effects/NamedEvaluation;
      // only later private copies disappear. The same observation lifetime
      // prevents stale copies across activation/TDZ/catch/loop boundaries.
      if(!coverage.writes[i]&&!use.escaped&&coverage.declarations[i]&&
          Confined(extents[i],lexical_[coverage.declarations[i]->value])) {
        roots[i]=symbol;constant_queue_[ready++]=symbol;continue;
      }
      if(coverage.writes[i]!=1||use.escaped||!coverage.covered_writes[i]||plan_.literals[i])continue;
      const auto& selected=*coverage.covered_writes[i];
      if(!use.private_storage || !use.declaration ||
          (selected.kind==FlowDefinitionKind::kAssignment && selected.effect!=FlowWriteEffect::kInert)) {
        // Keep observable writes and source/cell storage as roots. Coverage
        // permits copying their successful value, not deleting their completion.
        // A deleted constant store cannot provide retained storage.
        roots[i]=symbol;constant_queue_[ready++]=symbol;
        continue;
      }
      const auto rhs=selected.value;
      discard_rhs[i]=expressions.evaluations[rhs.value]==ExpressionEvaluation::kInert;
      // Normal-result identity is shared across sequence, choice and assignment
      // composition. Eliminating storage never authorizes dropping RHS effects.
      if(const auto copy=expressions.bindings[rhs.value]) {
        constant_next_[i]=constant_heads_[copy->value];constant_heads_[copy->value]=symbol;
      } else {roots[i]=symbol;constant_queue_[ready++]=symbol;}
    }
    std::size_t candidates=0;
    for(std::size_t at=0;at<ready;++at) {
      budget_.Step();const auto source=constant_queue_[at];
      for(auto target=constant_heads_[source.value];target;target=constant_next_[target->value]) {
        budget_.Step();RequireTree(!roots[target->value]&&ready<constant_queue_.size(),TreeStructureError::kOwnership);
        roots[target->value]=roots[source.value];constant_queue_[ready++]=*target;++candidates;
      }
    }
    if(!candidates)return;
    std::vector<std::optional<SymbolId>> copy_targets;
    std::vector<SourceRetirement> candidates_mask;
    std::vector<SourceRetirementQuery> references;
    std::vector<SourceRetentionDemand> scope_demands;
    budget_.Array(copy_targets,tree_.statements.size());budget_.Array(candidates_mask,uses_.size());
    budget_.Array(references,coverage.references.size());budget_.Array(scope_demands,coverage.references.size());
    for(std::size_t i=0;i<roots.size();++i) {
      budget_.Step();if(!roots[i]||roots[i]->value==i)continue;
      candidates_mask[i]=SourceRetirement::kProposed;
      // Only inert statement RHS reads vanish with their destination. An
      // embedded/observable RHS remains independently required after retirement.
      const auto statement=coverage.covered_writes[i]->statement;
      if(statement&&discard_rhs[i])copy_targets[statement->value]=SymbolId{static_cast<std::uint32_t>(i)};
    }
    std::size_t count=0,demand_count=0;
    for(const auto& read:coverage.references) {
      budget_.Step();if(read.kind!=FlowReferenceKind::kRead)continue;
      if(!roots[read.binding.value]||roots[read.binding.value]==read.binding)continue;
      RequireTree(read.owner.has_value(),TreeStructureError::kOwnership);
      const auto discarded=flow.events[read.point.value].discarded;
      const auto target=discarded?copy_targets[flow.discarded[discarded->value].statement.value]:std::nullopt;
      references[count++]={{*roots[read.binding.value],read.owner},read.binding,target};
      // Lexical containment remains independent of name resolution and TDZ.
      if(!flow.BindingInScope(*roots[read.binding.value],read.owner))scope_demands[demand_count++]={read.binding,target};
    }
    references.resize(count);scope_demands.resize(demand_count);
    auto analyzed=AnalyzeSourceNameRetirement(flow,candidates_mask,references,scope_demands,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    auto retirement=std::get<SourceNameRetirement>(std::move(analyzed));
    budget_.Admit(retirement.allocated_bytes,1);budget_.Step(retirement.work);
    // An owned delta survives invalidation. No shared fact is consulted after
    // BeginCommit; value/effect eligibility and common name closure are complete.
    for(std::size_t i=0;i<roots.size();++i) {
      budget_.Step();if(!roots[i]||roots[i]->value==i||retirement.presence[i]==SourceBindingPresence::kRetained)continue;
      const auto& selected=*coverage.covered_writes[i];const auto& use=uses_[i];
      BeginCommit();plan_.aliases[i]=roots[i];
      RemoveCoveredWrite(selected,*use.declaration);
      if(const auto& site=declaration_sites_[i])plan_.statements[site->statement.value].reset();
      if(selected.statement && discard_rhs[i]) {
        plan_.omitted[selected.statement->value]=1;plan_.statements[selected.statement->value].reset();
      }
    }
  }
  BindingInitializers Finish() {
    for (std::size_t i = 0; i < tree_.children.size(); ++i) {
      budget_.Step(); plan_.child_counts[i + 1] = plan_.child_counts[i] + !plan_.omitted[tree_.children[i].value];
    }
    plan_.allocated_bytes = budget_.Bytes(); plan_.work = budget_.Work(); return std::move(plan_);
  }
  void ObserveContainer(BindingUses& use, std::optional<StmtId> container) {
    if (container) {
      const auto at = containers_[container->value].begin;
      use.first_container = use.first_container ? std::min(*use.first_container, at) : at;
      use.last_container = use.last_container ? std::max(*use.last_container, at) : at;
    } else use.outside_body = true;
  }
  void Observe(SymbolId symbol, ExprId expression, std::optional<StmtId> container,
      std::optional<StmtId> projector) {
    auto& use = uses_[symbol.value]; if (!use.first) use.first = expression;
    if (projector) {
      RequireTree(projection_count_ < projection_observations_.size(), TreeStructureError::kOwnership);
      projection_observations_[projection_count_++] = {symbol, *projector};
    }
    else ObserveContainer(use, container);
  }
  void Capture(SymbolId symbol, ExprId occurrence, std::optional<StmtId> container,
      std::optional<StmtId> projector) {
    // Creation is the earliest possible observation through this live cell.
    // Never infer read-only from the parent's lack of writes; child calls may
    // mutate it. Counting creation before RHS completion protects TDZ/throws.
    Observe(symbol, occurrence, container, projector);
  }
  void Scan(ExprId root, std::optional<StmtId> container, std::optional<StmtId> projector) {
    std::size_t pending = 1; expressions_[0] = {root};
    while (pending) {
      budget_.Step(); const auto visit = expressions_[--pending];
      const auto& node = tree_.expressions[visit.expression.value].expression;
      if (!visit.finish) {
        expression_owners_[visit.expression.value]=projector?projector:container;
        expression_projections_[visit.expression.value]=projector.has_value();
        expressions_[pending++] = {visit.expression, true};
        const auto first_child=pending;
        VisitExpressionChildren(tree_, node, [&](ExprId child) {
          budget_.Step(); RequireTree(pending < expressions_.size(), TreeStructureError::kOwnership);
          expressions_[pending++] = {child,false};
        });
        // VisitExpressionChildren owns source evaluation order. A LIFO worklist
        // must reverse its pushed children, never evaluate arguments backwards.
        // Shared flow owns branch/activation and initialization facts. Here
        // RHS completion still precedes the original identifier PutValue.
        std::reverse(expressions_.begin()+first_child,expressions_.begin()+pending);
        if (const auto* read = std::get_if<SymbolExpr>(&node)) Observe(read->symbol, visit.expression, container, projector);
        for(const auto& capture:cells_.Captured(visit.expression,budget_))
          Capture(capture.binding,visit.expression,container,projector);
        if (const auto* update = std::get_if<UpdateExpr>(&node)) {
          Observe(update->symbol, visit.expression, container, projector);
        }
      } else {
        RequireTree(completion_count_ < completions_.size(), TreeStructureError::kOwnership);
        completions_[completion_count_++] = visit.expression;
        if (const auto* assignment = std::get_if<AssignmentExpr>(&node)) {
          if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) {
            Observe(target->symbol, visit.expression, container, projector);
            auto& use=uses_[target->symbol.value];
            const auto& rhs = tree_.expressions[assignment->value.value].expression;
            use.named_creation |= std::holds_alternative<FunctionExpr>(rhs) || std::holds_alternative<ClassExpr>(rhs);
          }
        }
      }
    }
  }
  void Enter(const StatementVisit& visit) {
    const auto& node = tree_.statements[visit.statement.value].statement;
    std::visit([&](const auto& syntax) {
      using T = std::decay_t<decltype(syntax)>;
      if constexpr (std::is_same_v<T, DeclarationStmt>) {
        if (syntax.initializer) Scan(*syntax.initializer, visit.lexical, visit.projector);
        const auto& symbol = tree_.symbols[syntax.symbol.value]; auto& use = uses_[syntax.symbol.value];
        if ((symbol.kind == BindingKind::kCompilerTemporary || symbol.kind == BindingKind::kLocal) &&
            (symbol.declaration == DeclarationKind::kLet || symbol.declaration == DeclarationKind::kConst) && visit.container && !visit.projected) {
          use.declaration = visit.statement;
          use.private_storage = symbol.kind == BindingKind::kCompilerTemporary;
          if(syntax.initializer) {
            const auto& rhs=tree_.expressions[syntax.initializer->value].expression;
            use.named_creation |= std::holds_alternative<FunctionExpr>(rhs)||std::holds_alternative<ClassExpr>(rhs);
          }
        }
      } else if constexpr (std::is_same_v<T, ExpressionStmt>) {
        Scan(syntax.expression, visit.lexical, visit.projector);
        const auto entry=loops_.initializer_owners[visit.statement.value];
        if((!visit.projected && visit.container) || entry)
          declaration_containers_[visit.statement.value]=entry?entry:visit.container;
        if (const auto* assignment = std::get_if<AssignmentExpr>(&tree_.expressions[syntax.expression.value].expression))
          if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) {
            auto& use = uses_[target->symbol.value];
            if(use.declaration && declaration_containers_[visit.statement.value] && !use.legacy_initializer)
              use.legacy_initializer=visit.statement;
          }
      } else if constexpr (std::is_same_v<T, IfStmt>) Scan(syntax.condition, visit.lexical, visit.projector);
      else if constexpr (std::is_same_v<T, ReturnStmt>) { if (syntax.value) Scan(*syntax.value, visit.lexical, visit.projector); }
      else if constexpr (std::is_same_v<T, ThrowStmt>) Scan(syntax.value, visit.lexical, visit.projector);
      else static_assert(std::is_same_v<T, BlockStmt> || std::is_same_v<T, TryStmt> || std::is_same_v<T, LoopStmt> ||
          std::is_same_v<T, BreakStmt> || std::is_same_v<T, ContinueStmt> || std::is_same_v<T, DebuggerStmt> || std::is_same_v<T, UnrecoveredStmt>);
    }, node);

  }
  const FunctionTree& tree_;
  const LoopSyntax& loops_;
  const NullishSyntax& nullish_;
  TreeCheckBudget& budget_;
  const OriginalFlow* original_;
  BindingInitializers plan_;
  std::vector<BindingUses> uses_;
  std::vector<std::optional<StmtId>> original_declarations_;
  std::vector<std::optional<DeclarationSite>> declaration_sites_;
  std::vector<std::optional<StmtId>> declaration_containers_;
  std::vector<std::uint8_t> projected_;
  std::vector<ContainerRange> containers_;
  std::vector<std::optional<StmtId>> container_parents_;
  std::vector<StmtId> container_order_;
  std::vector<std::optional<StmtId>> lexical_, projectors_;
  std::vector<std::optional<StmtId>> retained_bodies_;
  std::vector<ProjectionObservation> projection_observations_;
  std::size_t projection_count_ = 0;
  std::size_t next_container_ = 0;
  std::vector<ExpressionVisit> expressions_;
  std::vector<ExprId> completions_;
  std::size_t completion_count_ = 0;
  std::vector<std::optional<StmtId>> statement_owners_;
  std::vector<SourceSpellingId> spellings_;
  std::vector<std::optional<SourceSpellingId>> name_spellings_;
  std::vector<SourceRegion> source_regions_;
  std::vector<SourceBindingShadow> source_shadows_;
  std::vector<std::uint8_t> unique_spellings_;
  std::vector<std::optional<SourceRegionId>> source_scope_regions_;
  std::vector<SourceAllocation> source_allocations_;
  std::vector<LoopEntryOccurrence> loop_entry_occurrences_;
  std::vector<ScopeId> canonical_scopes_;
  std::vector<std::optional<SourcePosition>> source_occurrences_;
  std::vector<TextId> literal_texts_;
  std::vector<std::optional<PrimitiveId>> expression_primitives_;
  PrimitiveId undefined_value_;
  std::vector<FlowValueEquation> value_equations_;
  SourceCells cells_;
  std::vector<std::uint8_t> stable_entries_;
  bool mapped_arguments_=false;
  std::vector<std::optional<StmtId>> expression_owners_;
  std::vector<std::uint8_t> expression_projections_;
  std::vector<std::optional<SymbolId>> constant_heads_, constant_next_;
  std::vector<SymbolId> constant_queue_;
  std::vector<StatementVisit> statements_;
  RecoverySchedule schedule_;
  mutable std::optional<ProjectionFacts> facts_;
};
}
std::variant<BindingInitializers, TreeStructureError> PlanBindingInitializers(
    const FunctionTree& tree, const LoopSyntax& loops, const NullishSyntax& nullish, analysis::SemanticBudget budget,const OriginalFlow* original) {
  try {
    TreeCheckBudget checked(budget); return Initializers(tree, loops, nullish, checked,original).Run();
  } catch (TreeStructureError error) { return error;
  } catch (const std::bad_alloc&) { return TreeStructureError::kResourceLimit;
  } catch (const std::length_error&) { return TreeStructureError::kResourceLimit; }
}
}
