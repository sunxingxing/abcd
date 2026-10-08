#include "source_names.h"
#include "source_name_retirement.h"
#include "binding_initializer.h"
#include "recovery_schedule.h"
#include "binding_interference.h"
#include "storage_coalescing.h"
#include "definition_elision.h"
#include "flow_dominance.h"
#include "flow_definitions.h"
#include "flow_constants.h"
#include "primitive_value.h"
#include "flow_motion.h"
#include "class_tree.h"
#include "expression_children.h"
#include "loop_syntax.h"
#include "loop_declarations.h"
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
};
class Initializers {
 public:
  Initializers(const FunctionTree& tree, const LoopSyntax& loops, const NullishSyntax& nullish,
      TreeCheckBudget& budget) : tree_(tree), loops_(loops), nullish_(nullish), budget_(budget) {
    budget.Array(plan_.statements, tree.statements.size()); budget.Array(plan_.omitted, tree.statements.size());
    budget.Array(plan_.loop_declarations, tree.statements.size());
    budget.Array(plan_.forwarded_values, tree.statements.size());
    budget.Array(plan_.literals, tree.symbols.size());
    budget.Array(plan_.read_literals, tree.expressions.size());
    budget.Array(plan_.read_bindings,tree.symbols.size());
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
    for (auto symbol : tree_.closure_captures) { budget_.Step(); uses_[symbol.value].escaped = true; }
    for (const auto& alias : tree_.modules) {
      budget_.Step(); uses_[alias.symbol.value].escaped = true; uses_[alias.symbol.value].module_alias = true;
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
          if(const auto* block=std::get_if<BlockStmt>(&node))
            source_scope_regions_[block->scope.value]=SourceRegionId{visit.statement.value};
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
    }
    for(auto& owner:statement_owners_) {budget_.Step();if(owner)owner=lexical_[owner->value];}
    // Syntax recovery owns source region construction once. Publish current
    // occurrence ownership to the shared flow; consumers never infer lifetime
    // from a declaration's old arena position or perform another scope census.
    for(std::size_t i=0;i<tree_.statements.size();++i) {
      budget_.Step();source_regions_[i]={{containers_[i].begin},{containers_[i].end}};
      if(statement_owners_[i])source_occurrences_[i]=SourcePosition{containers_[statement_owners_[i]->value].begin};
    }
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
    bool simple=true;for(const auto& parameter:tree_.parameters){budget_.Step();simple&=!parameter.initializer&&!parameter.rest;}
    mapped_arguments_=tree_.strictness==Strictness::kSloppy&&!IsArrowCallable(tree_.callable)&&simple;
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
    DemandCleanup();
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
    EnsureDemandCleanup();
    // Place initialization after value normalization, before producer coverage.
    // Existing dominance/extent proofs consume the current retained projection.
    RunPass(Pass::kDeclarations);
    EnsureDemandCleanup();
    RunPass(Pass::kAliases);
    RunPass(Pass::kStoredResults);
    RunPass(Pass::kInertOperands);
    RunPass(Pass::kInertStatements);
    EnsureDemandCleanup();
    schedule_.EnterStorage();
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
    EnsureDemandCleanup();
    // Storage/alias cleanup can remove names or observations that blocked the
    // earlier snapshot attempt. Reconsider once at this stable stage boundary,
    // with fresh versioned facts. No whole-function retry loop or round cap.
    if(schedule_.SnapshotRefreshNeeded()) {
      RunPass(Pass::kSnapshots);
      RunPass(Pass::kStoredResults);
      RunPass(Pass::kInertOperands);
      RunPass(Pass::kInertStatements);
      EnsureDemandCleanup();
    }
    // Stable binding values consume final storage/snapshot read destinations.
    // Doing this before motion can choose a name unavailable at that destination.
    schedule_.EnterReadValues();
    RunPass(Pass::kBindingValues);
    EnsureDemandCleanup();
    RunPass(Pass::kInertOperands);
    EnsureDemandCleanup();
    // Final source grammar consumes the final bindings/evaluations. It can
    // retain outlined prefix effects without reviving removed declarations.
    schedule_.EnterGrammar();
    RunPass(Pass::kLoopDeclarations);
    RunPass(Pass::kDeclarationKinds);
    for (std::size_t i = 0; i < loops_.initializer_roots.size(); ++i) {
      budget_.Step(); plan_.entry_counts[i + 1] = plan_.entry_counts[i] +
          !plan_.omitted[loops_.initializer_roots[i].statement.value];
    }
    AnalyzeExpressions();
    plan_.normal_stores = std::move(CurrentFacts().expressions->normal_stores);
    plan_.evaluations = std::move(CurrentFacts().expressions->evaluations);
    return Finish();
  }
 private:
  using Pass=RecoveryPass;
  struct ExpressionFacts {
    std::vector<std::optional<SymbolId>> bindings;
    // Primitive normal-result identity is independent of retained evaluation.
    // IDs denote values in the immutable primitive pool, not occurrences.
    std::vector<std::optional<PrimitiveId>> literals;
    // Effective expression identity after the current replacement plan. Each
    // consumer uses this shared normalization instead of chasing old syntax.
    std::vector<ExprId> roots;
    std::vector<std::uint8_t> normal_stores;
    std::vector<ExpressionEvaluation> evaluations;
    std::vector<LexicalDependency> lexical;
  };
  // All event-indexed and expression facts belong to one immutable projection.
  // A commit destroys this aggregate, so adding an analysis cannot accidentally
  // omit its invalidation. Only explicitly transported initialization proofs
  // live outside it; reusable flow capacity contains no published facts.
  struct ProjectionFacts {
    ProjectionRevision revision;
    std::optional<FlowInitializations> initializations;
    std::optional<ExpressionFacts> expressions;
    std::optional<RecoveryFlow> flow;
    std::optional<FlowLiveness> liveness;
    std::optional<FlowDemand> demand;
    std::optional<FlowConstants> constants;
  };
  ProjectionFacts& CurrentFacts() const {
    RequireAnalysis();
    if(!facts_)facts_.emplace(ProjectionFacts{schedule_.Revision(),{},{},{},{},{},{}});
    schedule_.RequireCurrent(facts_->revision);
    return *facts_;
  }
  // The first write enters a one-way commit phase. Analysis cannot observe a
  // partly applied batch, and scheduling derives changes from the revision,
  // never a hand-maintained boolean returned by an individual pass.
  bool RunPass(Pass pass) {
    schedule_.Start(pass);
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
      case Pass::kInertStatements:OmitInertStatements();break;
      case Pass::kInertOperands:ElideInertOperands();break;
      case Pass::kStorage:Coalesce();break;
    }
    return schedule_.Finish();
  }
  void DemandCleanup() {
    RunPass(Pass::kDeadDefinitions);
    RunPass(Pass::kInitialValues);
    RunPass(Pass::kBindings);
  }
  void EnsureDemandCleanup() {
    if(schedule_.DemandCleanupNeeded())DemandCleanup();
  }
  void BeginCommit() {
    if(!schedule_.Commit())return;
    if(facts_ && facts_->flow)flow_storage_=std::move(*facts_->flow);
    facts_.reset();
    if(!schedule_.PreservesInitialization())initialized_.reset();
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
    struct DeclarationEdit {
      DeclarationSite site;
      BindingInitializer initializer;
      std::optional<StmtId> omitted;
    };
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
          {SymbolId{static_cast<std::uint32_t>(i)}, *assignment, DeclarationKind::kLet},
          loops_.initializer_owners[site->statement.value] ? std::nullopt : use.declaration};
    }
    // Placement is projection state, separate from the immutable use census.
    // Publish it with the same revision/invalidation boundary as the plan.
    for(std::size_t i=0;i<count;++i) {
      budget_.Step();const auto& edit=proposed[i];BeginCommit();
      declaration_sites_[edit.initializer.symbol.value]=edit.site;
      plan_.statements[edit.site.statement.value]=edit.initializer;
      if(edit.omitted)DiscardDeclaration(*edit.omitted);
    }
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
  void ProjectLoopDeclarations() {
    RequireAnalysis();
    if(loops_.initializer_roots.empty())return;
    std::vector<std::optional<StmtId>> declarations;
    budget_.Array(declarations,uses_.size());
    for(std::size_t i=0;i<uses_.size();++i){budget_.Step();declarations[i]=uses_[i].declaration;}
    const auto planned=PlanLoopDeclarations(loops_,plan_,declarations,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&planned))throw *error;
    const auto& delta=std::get<LoopDeclarations>(planned);
    budget_.Admit(delta.allocated_bytes,1);budget_.Step(delta.work);
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
    // Replacement preserves GetValue and suppresses NamedEvaluation. Printer
    // consumes this decision and only projects the removed origin ranges.
    std::size_t capacity=0;
    for(std::size_t i=0;i<tree_.expressions.size();++i) {
      budget_.Step();if(!plan_.replacements[i] &&
          std::holds_alternative<SequenceExpr>(tree_.expressions[i].expression))++capacity;
    }
    if(!capacity)return;
    const auto& facts=AnalyzeExpressions();
    const RecoveryFlow* flow=nullptr;
    struct OperandElision { ExprId expression,value; };
    std::vector<OperandElision> proposed;budget_.Array(proposed,capacity);std::size_t count=0;
    for(std::size_t i=0;i<tree_.expressions.size();++i) {
      budget_.Step();if(plan_.replacements[i])continue;
      const auto* sequence=std::get_if<SequenceExpr>(&tree_.expressions[i].expression);
      if(!sequence)continue;
      const ExprId id{static_cast<std::uint32_t>(i)};
      if(facts.evaluations[sequence->left.value]==ExpressionEvaluation::kInert)
        proposed[count++]={id,sequence->right};
      else if(facts.evaluations[sequence->right.value]==ExpressionEvaluation::kInert) {
        if(!flow)flow=&CurrentFlow();
        if(flow->value_uses[i]==FlowValueUse::kDiscarded)proposed[count++]={id,sequence->left};
      }
    }
    for(std::size_t i=0;i<count;++i) {
      budget_.Step();const auto& edit=proposed[i];BeginCommit();
      plan_.replacements[edit.expression.value]=edit.value;
    }
  }
  void OmitInertStatements() {
    const auto& facts = AnalyzeExpressions();
    const auto omitted = PlanInertStatements(facts);
    // The pass publishes a complete proposal before changing the projection.
    // No subsequent statement observes a partly applied omission batch.
    for (const auto statement : omitted) {
      budget_.Step(); BeginCommit();plan_.omitted[statement.value] = 1;
    }
  }
  std::size_t ProjectionCapacity() {
    // Each expression has at most one direct binding observation. Capture
    // edges are separate owned inputs: one creation can expose many cells.
    // Count those edges once; do not cap them by expression count or rescan
    // captures for each binding. The same bound covers class member families.
    auto count = tree_.expressions.size();
    const auto add = [&](std::size_t edges) {
      RequireTree(edges <= std::numeric_limits<std::size_t>::max() - count, TreeStructureError::kResourceLimit);
      count += edges;
    };
    const auto family = [&](const std::optional<PreparedFamily>& prepared) {
      if (prepared) {
        const auto& members = prepared->Family(); add(members.members[members.root.value].tree.captures.size());
      }
    };
    add(tree_.closure_captures.size());
    for (const auto& expression : tree_.expressions) {
      budget_.Step(); const auto* created = std::get_if<ClassExpr>(&expression.expression); if (!created) continue;
      const auto& definition = *created->definition; family(definition.constructor);
      for (const auto& member : definition.members) { budget_.Step(); family(member); }
      for (const auto& site : definition.definitions) for (const auto& body : site.bodies) { budget_.Step(); family(body); }
      if (definition.initializer) add(definition.initializer->tree.captures.size());
    }
    return count;
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
    const auto& expressions=AnalyzeExpressions();
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
        auto analyzed=AnalyzeFlowMotion(flow,budget_.Remaining());
        if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
        motion=std::get<FlowMotion>(std::move(analyzed));
        budget_.Admit(motion->allocated_bytes,1);budget_.Step(motion->work);
      }
      return *motion;
    };
    struct ForwardedSnapshot {SymbolId binding;ExprId read,value;std::optional<ExprId> expression;StmtId statement,owner;std::optional<StmtId> initialization;};
    std::vector<ForwardedSnapshot> proposed;
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
      proposed[count++]={SymbolId{static_cast<std::uint32_t>(i)},read,rhs,definition.write,statement,
          *old_owner,flow.bindings[i].declaration};
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
    // Evaluate the whole proposed projection WITHOUT mutating the plan. Shared
    // flow assigns actual owners after composed/nested transports; checking old
    // statement owners would move siblings or miss final destination scopes.
    std::vector<std::optional<ExprId>> replacements;
    std::vector<std::optional<ExpressionTransport>> transports;
    std::vector<std::optional<SymbolId>> transported_reads;
    std::vector<std::uint8_t> omitted;
    budget_.Array(replacements,plan_.replacements.size());budget_.Array(transports,plan_.transports.size());
    budget_.Array(omitted,plan_.omitted.size());
    budget_.Array(transported_reads,tree_.expressions.size());
    budget_.Step(replacements.size()+transports.size()+omitted.size());
    std::copy(plan_.replacements.begin(),plan_.replacements.end(),replacements.begin());
    std::copy(plan_.transports.begin(),plan_.transports.end(),transports.begin());
    std::copy(plan_.omitted.begin(),plan_.omitted.end(),omitted.begin());
    for(std::size_t i=0;i<count;++i) {
      budget_.Step();const auto& action=proposed[i];replacements[action.read.value]=action.value;
      transported_reads[action.read.value]=action.binding;
      if(action.expression)transports[action.expression->value]=ExpressionTransport{action.value,undefined_value_};
      else omitted[action.statement.value]=1;
    }
    const auto projected=BuildProjectedFlow(expressions,replacements,transports,omitted,{},transported_reads);
    std::vector<std::uint8_t> invalid;budget_.Array(invalid,uses_.size());
    std::vector<SourceNameQuery> queries;std::vector<SymbolId> roots;
    budget_.Array(queries,projected.references.size());budget_.Array(roots,projected.references.size());
    std::size_t size=0;
    for(const auto& reference:projected.references) {
      budget_.Step();const auto* expression=std::get_if<ExprId>(&reference.site);
      if(!expression)continue;const auto root=projected.expression_transports[expression->value];
      if(!root)continue;
      if(!reference.owner){invalid[root->value]=1;continue;}const auto owner=*reference.owner;
      if(!flow.BindingInScope(reference.binding,owner))invalid[root->value]=1;
      queries[size]={reference.binding,owner};roots[size++]=*root;
    }
    queries.resize(size);roots.resize(size);
    const auto resolved=ResolveNames(projected,queries);
    for(std::size_t i=0;i<size;++i){budget_.Step();if(resolved[i]!=SourceNameResolution::kResolved)invalid[roots[i].value]=1;}
    std::vector<SourceNameQuery> names;std::vector<SymbolId> name_roots;
    budget_.Array(names,projected.names.size());budget_.Array(name_roots,projected.names.size());size=0;
    for(const auto& reference:projected.names) {
      budget_.Step();const auto root=projected.expression_transports[reference.site.value];
      if(!root)continue;
      if(!reference.owner){invalid[root->value]=1;continue;}const auto owner=*reference.owner;
      names[size]={reference.name,owner};name_roots[size++]=*root;
    }
    names.resize(size);name_roots.resize(size);
    const auto resolved_names=ResolveNames(projected,names);
    for(std::size_t i=0;i<size;++i){budget_.Step();if(resolved_names[i]!=SourceNameResolution::kResolved)invalid[name_roots[i].value]=1;}
    std::vector<std::uint8_t> blocked;budget_.Array(blocked,count);
    for(std::size_t i=0;i<count;++i) {
      budget_.Step();const auto& action=proposed[i];const auto owner=projected.expression_owners[action.value.value];
      const auto root=projected.expression_transports[action.value.value];
      if(!root){blocked[i]=1;continue;}
      if(!owner||!statement_owners_[owner->value]){invalid[root->value]=1;continue;}
      const auto environment=statement_owners_[owner->value];
      if(environments[action.owner.value]!=environments[environment->value] &&
          (ResourceFailures(tree_.identity.contract)==ResourceFailureScope::kObservable ||
           expressions.lexical[action.value.value]==LexicalDependency::kEnclosingEnvironment))invalid[root->value]=1;
    }
    // A nested transport can rely on an outer transport's final environment.
    // The common adapter records that dependency, independently of statement
    // layout. Retain the affected component; unrelated moves in the same
    // statement must not inherit its failure or a hypothetical owner as proof.
    for(std::size_t i=0;i<count;++i) {
      budget_.Step();const auto root=projected.expression_transports[proposed[i].value.value];
      if(root)blocked[i]|=invalid[root->value];
    }
    // Value availability, motion and lexical proofs all observe the same
    // projection. Apply only after all proposals are complete; later passes
    // must reconstruct facts for the forwarded expression ownership.
    for(std::size_t i=0;i<count;++i) {
      budget_.Step();if(blocked[i])continue;const auto& action=proposed[i];const auto& use=uses_[action.binding.value];
      BeginCommit();plan_.replacements[action.read.value]=action.value;
      if(action.expression)plan_.transports[action.expression->value]=ExpressionTransport{action.value,undefined_value_};
      else plan_.forwarded_values[action.statement.value]=action.value;
      DiscardDeclaration(*use.declaration);
      if(action.initialization && *action.initialization!=action.statement &&
          plan_.declaration_completions[action.initialization->value])DiscardDeclaration(*action.initialization);
      if(!action.expression)plan_.omitted[action.statement.value]=1;
      // An occurrence can sit inside another binding's initializer. Remove
      // only completion/declaration actions owned by this elided binding.
      if(auto& declaration=plan_.statements[action.statement.value];declaration&&declaration->symbol==action.binding)
        declaration.reset();
      if(auto& completion=plan_.declaration_completions[action.statement.value];completion&&completion->binding==action.binding)
        completion.reset();
    }
  }
  std::vector<SourceNameResolution> ResolveNames(const RecoveryFlow& flow,std::span<const SourceNameQuery> queries) const {
    RequireAnalysis();auto analyzed=ResolveSourceNames(flow,queries,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    auto result=std::get<SourceNameResults>(std::move(analyzed));
    budget_.Admit(result.allocated_bytes,1);budget_.Step(result.work);return std::move(result.resolutions);
  }
  RecoveryFlow BuildProjectedFlow(const ExpressionFacts& expressions,
      std::span<const std::optional<ExprId>> replacements,
      std::span<const std::optional<ExpressionTransport>> transports,
      std::span<const std::uint8_t> omitted,RecoveryFlow storage={},
      std::span<const std::optional<SymbolId>> transported_reads={}) const {
    RequireAnalysis();
    std::vector<std::uint8_t> private_bindings;
    std::vector<SourceBindingScope> source_bindings;
    budget_.Array(private_bindings,uses_.size());
    budget_.Array(source_bindings,uses_.size());
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
      const auto& site=declaration_sites_[i];
      if(site&&plan_.statements[site->statement.value])source_bindings[i]=ProjectedBindingScope{SourceRegionId{site->container.value}};
    }
    const LegacyFlowProjection projection{plan_.aliases,plan_.literals,replacements,
        omitted,expressions.evaluations,expressions.normal_stores,plan_.statements,private_bindings,
        plan_.storage,plan_.undefined_stores,plan_.declaration_values,plan_.declaration_completions,
        {source_regions_,source_occurrences_,source_bindings,source_shadows_,unique_spellings_,spellings_,name_spellings_},plan_.read_literals,transports,transported_reads,plan_.read_bindings,stable_entries_};
    auto built=BuildRecoveryFlow(tree_,projection,budget_.Remaining(),std::move(storage));
    if(const auto* error=std::get_if<TreeStructureError>(&built))throw *error;
    auto flow=std::get<RecoveryFlow>(std::move(built));
    budget_.Admit(flow.allocated_bytes,1);budget_.Step(flow.work);return flow;
  }
  const RecoveryFlow& CurrentFlow() const {
    schedule_.RequireFact(RecoveryFact::kFlow);
    auto& facts=CurrentFacts();
    if(facts.flow)return *facts.flow;
    // Bootstrap without initialization assumptions. Completion dominance on
    // this conservative graph proves facts before effect summaries are refined.
    // Unmodeled signature-program occurrences receive no body-entry proof.
    const auto build=[&](const ExpressionFacts& expressions) {
      return BuildProjectedFlow(expressions,plan_.replacements,plan_.transports,plan_.omitted,std::move(flow_storage_));
    };
    if(initialized_) {
      if(!facts.expressions)facts.expressions=ComputeExpressions(*initialized_);
      facts.flow=build(*facts.expressions);return *facts.flow;
    }
    const auto conservative=ComputeExpressions({});
    auto provisional=build(conservative);
    auto analyzed=AnalyzeFlowInitializations(provisional,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    // Bootstrap proofs justify removing impossible exceptional edges only.
    // Do not publish their conservative dominance as the refined graph cache.
    const auto initialization=std::get<FlowInitializations>(std::move(analyzed));
    budget_.Admit(initialization.allocated_bytes,1);budget_.Step(initialization.work);
    initialized_.emplace();auto& initialized=*initialized_;
    budget_.Array(initialized,tree_.expressions.size());
    for(const auto& reference:provisional.references) {
      budget_.Step();if(const auto* expression=std::get_if<ExprId>(&reference.site))
        initialized[expression->value]=initialization.Completed(reference.binding,reference.point);
    }
    facts.expressions=ComputeExpressions(initialized);
    auto refined=RefineRecoveryFlowEffects(provisional,tree_,facts.expressions->evaluations,
        facts.expressions->normal_stores,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&refined))throw *error;
    const auto& refinement=std::get<FlowRefinement>(refined);
    budget_.Admit(refinement.allocated_bytes,1);budget_.Step(refinement.work);
    facts.flow=std::move(provisional);return *facts.flow;
  }
  const FlowLiveness& CurrentLiveness() const {
    schedule_.RequireFact(RecoveryFact::kLiveness);
    auto& facts=CurrentFacts();
    if(facts.liveness)return *facts.liveness;
    const auto& flow=CurrentFlow();
    auto analyzed=AnalyzeFlowLiveness(flow,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    facts.liveness=std::get<FlowLiveness>(std::move(analyzed));
    budget_.Admit(facts.liveness->allocated_bytes,1);budget_.Step(facts.liveness->work);return *facts.liveness;
  }
  const FlowDemand& CurrentDemand() const {
    schedule_.RequireFact(RecoveryFact::kDemand);
    auto& facts=CurrentFacts();
    if(!facts.demand) {
      auto analyzed=AnalyzeFlowDemand(CurrentFlow(),budget_.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
      facts.demand=std::get<FlowDemand>(std::move(analyzed));
      budget_.Admit(facts.demand->allocated_bytes,1);budget_.Step(facts.demand->work);
    }
    return *facts.demand;
  }
  const FlowInitializations& CurrentInitializations() const {
    schedule_.RequireFact(RecoveryFact::kInitializations);
    auto& facts=CurrentFacts();
    const auto& flow=CurrentFlow();
    // Consumers need dominance of the exact current graph. Bootstrap positives
    // remain sound after refinement, but its negative answers can become stale.
    if(!facts.initializations) {
      auto analyzed=AnalyzeFlowInitializations(flow,budget_.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
      facts.initializations=std::get<FlowInitializations>(std::move(analyzed));
      budget_.Admit(facts.initializations->allocated_bytes,1);budget_.Step(facts.initializations->work);
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
    // Retire only binding completion. An earlier pass may already have outlined
    // its independent RHS; declaration removal never authorizes deleting it.
    if(const auto completion=plan_.declaration_completions[statement.value]) {
      if(completion->evaluation) {
        plan_.declaration_values[statement.value]=DeclarationValue{*completion->evaluation,{}};
        plan_.omitted[statement.value]=0;
      }else plan_.omitted[statement.value]=1;
      plan_.declaration_completions[statement.value].reset();
    }else if(!plan_.declaration_values[statement.value])plan_.omitted[statement.value]=1;
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
    // Initialization preservation is member-local with fixed representatives.
    // Its owned actions remain valid if name/lifetime closure retains a subset;
    // there is no graph or plan mutation between these independent analyses.
    auto initialized=PlanStorageInitializations(flow,liveness,initialization_order,proposed,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&initialized))throw *error;
    const auto& initialization=std::get<StorageInitializations>(initialized);
    budget_.Admit(initialization.allocated_bytes,1);budget_.Step(initialization.work);
    std::vector<SourceRetirement> candidates_mask;
    std::vector<SourceRetirementQuery> references;
    std::vector<SourceRetentionDemand> demands;
    RequireTree(flow.references.size()<=std::numeric_limits<std::size_t>::max()-flow.definitions.size(),TreeStructureError::kResourceLimit);
    const auto occurrences=flow.references.size()+flow.definitions.size();
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
      if(!initialization_order.Completed(*root,reference.point)||
          !initialization_order.Completed(reference.binding,reference.point)||
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
  bool MutableBinding(SymbolId symbol) const {
    // Only a native writable declarative binding has an unobservable PutValue.
    // Import/capture/self bindings need their own mutability/environment proof;
    // a property target can invoke a setter even when the value is unchanged.
    const auto& binding = tree_.symbols[symbol.value];
    if (binding.declaration == DeclarationKind::kConst) return false;
    switch (binding.kind) {
      case BindingKind::kLocal: case BindingKind::kCompilerTemporary:
      case BindingKind::kParameter: case BindingKind::kCatchParameter: return true;
      case BindingKind::kCapture: case BindingKind::kModule:
      case BindingKind::kFunctionSelf: return false;
    }
    throw TreeStructureError::kBinding;
  }
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
    auto& facts=CurrentFacts();
    if(!facts.expressions) {
      if(initialized_)facts.expressions=ComputeExpressions(*initialized_);
      else CurrentFlow();
    }
    return *facts.expressions;
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
  FlowValueEquation ProjectedValueEquation(ExprId id) const {
    RequireAnalysis();
    if(const auto transport=plan_.transports[id.value])return FlowPrimitiveReference{transport->replacement};
    if(const auto replacement=plan_.replacements[id.value])return FlowExpressionValue{*replacement};
    if(const auto primitive=plan_.read_literals[id.value])return FlowPrimitiveReference{*primitive};
    auto equation=value_equations_[id.value];
    if(const auto* read=std::get_if<FlowBindingValue>(&equation)) {
      return ProjectedBindingValue(read->binding);
    }
    return equation;
  }
  const TreeLiteral& LiteralValue(PrimitiveId literal) const {return plan_.primitives[literal.value];}
  bool SameLiteral(PrimitiveId left,PrimitiveId right) const {return SamePrimitiveValue(LiteralValue(left),LiteralValue(right));}
  bool LiteralTruth(PrimitiveId literal) const {return PrimitiveTruth(LiteralValue(literal),tree_.texts);}
  bool LiteralNullish(PrimitiveId literal) const {return PrimitiveNullish(LiteralValue(literal));}
  ExpressionFacts ComputeExpressions(std::span<const std::uint8_t> initialized) const {
    RequireAnalysis();
    ExpressionFacts facts;
    budget_.Array(facts.bindings, tree_.expressions.size());
    budget_.Array(facts.literals, tree_.expressions.size());
    budget_.Array(facts.roots, tree_.expressions.size());
    budget_.Array(facts.normal_stores, tree_.expressions.size());
    budget_.Array(facts.evaluations, tree_.expressions.size());
    budget_.Array(facts.lexical,tree_.expressions.size());
    auto& stored = facts.bindings;
    // All facts describe one unchanged projection. The analysis is const and
    // publishes no plan state; proposal/commit consumers own all mutations.
    // Scan's finish order is postorder even when expression IDs are reversed.
    // Each owned child is consumed once. No repeated subtree/type/CFG walk.
    for (std::size_t at = 0; at < completion_count_; ++at) {
      budget_.Step(); const auto id = completions_[at];
      const auto& node = tree_.expressions[id.value].expression;
      // Normal-completion fact: the result is still the value of this binding.
      // ECMA-262 assignment/prefix update write before returning that value;
      // comma forwards its right completion, and choice joins must agree.
      // Unknown calls/coercions/suspensions produce no fact. No effect moves,
      // and a throwing/TDZ RHS still executes before any projected outer store.
      // Use the existing finish order, not arena order or a local syntax match.
      auto& result = stored[id.value];facts.roots[id.value]=id;
      auto& literal=facts.literals[id.value];
      const auto join = [&](ExprId left, ExprId right) -> std::optional<SymbolId> {
        return stored[left.value] == stored[right.value] ? stored[left.value] : std::nullopt;
      };
      if(const auto transport=plan_.transports[id.value]) {
        literal=transport->replacement;facts.evaluations[id.value]=ExpressionEvaluation::kInert;
        facts.lexical[id.value]=LexicalDependency::kReferences;
        continue;
      }
      if (const auto replacement = plan_.replacements[id.value]) {
        facts.roots[id.value]=facts.roots[replacement->value];
        result = stored[replacement->value];
        literal=facts.literals[replacement->value];
        facts.evaluations[id.value] = facts.evaluations[replacement->value];
        facts.lexical[id.value]=facts.lexical[replacement->value];
        continue;
      }
      const auto equation=ProjectedValueEquation(id);
      auto evaluation = IntrinsicEvaluation(node);
      auto lexical=IntrinsicLexicalDependency(node);
      if (const auto* read = std::get_if<SymbolExpr>(&node)) {
        literal=plan_.ReadLiteral(id,read->symbol);
        if (!literal) {
          const auto source = plan_.ReadBinding(read->symbol);
          result = source;
        }
        if (literal || (!initialized.empty() && initialized[id.value]))
          evaluation = ExpressionEvaluation::kInert;
      } else if (const auto* assignment = std::get_if<AssignmentExpr>(&node)) {
        literal=facts.literals[assignment->value.value];
        if (const auto* target = std::get_if<SymbolTarget>(&assignment->target);
            target && MutableBinding(target->symbol)) {
          result = plan_.Storage(target->symbol);
          facts.normal_stores[id.value] = !initialized.empty() && initialized[id.value];
        }
      } else if (const auto* update = std::get_if<UpdateExpr>(&node)) {
        if (update->result == UpdateResult::kPrefix && MutableBinding(update->symbol)) result = plan_.Storage(update->symbol);
      } else if (const auto* sequence = std::get_if<SequenceExpr>(&node)) result = stored[sequence->right.value];
      else if (const auto* choice = std::get_if<ConditionalExpr>(&node)) result = join(choice->consequent, choice->alternate);
      else if (const auto* logical = std::get_if<LogicalExpr>(&node)) result = join(logical->left, logical->right);
      else if (const auto* receiver = std::get_if<ThisExpr>(&node)) {
        if (receiver->owner == tree_.owner && tree_.callable != CallableKind::kDerivedConstructor &&
            !IsArrowCallable(tree_.callable)) evaluation = ExpressionEvaluation::kInert;
      }
      // ECMA-262 completion values: sequence/assignment forward a value;
      // conditional joins equal values or selects a known primitive condition;
      // logical selection uses ToBoolean/nullish, never user coercion. Unknown
      // operators/calls/updates/suspensions retain no primitive fact. Effects,
      // initialization and source ownership are still analyzed separately.
      std::visit([&](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,FlowPrimitiveReference>)literal=value.primitive;
        else if constexpr(std::is_same_v<T,FlowExpressionValue>)literal=facts.literals[value.expression.value];
        else if constexpr(std::is_same_v<T,FlowChoiceValue>) {
          const auto a=facts.literals[value.consequent.value],b=facts.literals[value.alternate.value];
          if(const auto condition=facts.literals[value.condition.value]) {
            const auto selected=LiteralTruth(*condition)?value.consequent:value.alternate;
            literal=facts.literals[selected.value];result=stored[selected.value];
          } else if(a&&b&&SameLiteral(*a,*b))literal=a;
        } else if constexpr(std::is_same_v<T,FlowLogicalValue>) {
          if(const auto left=facts.literals[value.left.value]) {
            bool right=false;
            switch(value.operation) {
              case TreeLogical::kAnd:right=LiteralTruth(*left);break;
              case TreeLogical::kOr:right=!LiteralTruth(*left);break;
              case TreeLogical::kNullish:right=LiteralNullish(*left);break;
            }
            literal=right?facts.literals[value.right.value]:left;
            result=stored[(right?value.right:value.left).value];
          }
        } else static_assert(std::is_same_v<T,UnknownFlowValue>||std::is_same_v<T,FlowBindingValue>);
      },equation);
      VisitExpressionChildren(tree_, node, [&](ExprId child) {
        budget_.Step(); if (facts.evaluations[child.value] == ExpressionEvaluation::kRequired)
          evaluation = ExpressionEvaluation::kRequired;
        switch(facts.lexical[child.value]) {
          case LexicalDependency::kReferences:break;
          case LexicalDependency::kCallableOwner:
            if(lexical==LexicalDependency::kReferences)lexical=LexicalDependency::kCallableOwner;
            break;
          case LexicalDependency::kEnclosingEnvironment:lexical=LexicalDependency::kEnclosingEnvironment;break;
        }
      });
      facts.evaluations[id.value] = evaluation;facts.lexical[id.value]=lexical;
    }
    return facts;
  }
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
  std::vector<StmtId> PlanInertStatements(const ExpressionFacts& facts) const {
    std::vector<StmtId> omitted;
    budget_.Array(omitted, tree_.statements.size()); std::size_t count = 0;
    // Expression statements discard their value. Shared initialization and
    // effect facts prove omission, independently of whether an earlier pass
    // replaced their root. Keep declarations and all required evaluations;
    // TDZ, coercion, calls and suspension are not inert merely because unused.
    for (std::size_t i = 0; i < tree_.statements.size(); ++i) {
      budget_.Step(); const auto* effect = std::get_if<ExpressionStmt>(&tree_.statements[i].statement);
      const auto& value=plan_.declaration_values[i];
      if (!plan_.omitted[i] && !plan_.declaration_completions[i] && !plan_.statements[i] &&
          (value ? !value->target && facts.evaluations[value->value.value]==ExpressionEvaluation::kInert :
              effect && facts.evaluations[effect->expression.value] == ExpressionEvaluation::kInert))
        omitted[count++] = StmtId{static_cast<std::uint32_t>(i)};
    }
    omitted.resize(count); return omitted;
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
      const FlowValueEquations expressions{value_equations_,plan_.replacements,bindings,*initialized_,plan_.read_literals,{},plan_.transports};
      std::vector<FlowDefinitionValue> values;budget_.Array(values,flow.definitions.size());
      for(std::size_t i=0;i<values.size();++i) {
        budget_.Step();const auto& definition=flow.definitions[i];
        switch(definition.kind) {
          case FlowDefinitionKind::kUndefined:case FlowDefinitionKind::kUndefinedStore:
            values[i]=FlowPrimitiveValue{LiteralValue(undefined_value_),undefined_value_};break;
          case FlowDefinitionKind::kAssignment:case FlowDefinitionKind::kInitializer:
            if(definition.value)values[i]=FlowExpressionValue{*definition.value};
            break;
          // Update computes a new numeric value; its stored-result identity
          // is not a transparent self-copy equation. Entry/external values
          // remain unknown until their independent semantics are modeled.
          case FlowDefinitionKind::kUpdate:case FlowDefinitionKind::kParameter:
          case FlowDefinitionKind::kCatch:case FlowDefinitionKind::kFunctionSelf:break;
        }
      }
      auto analyzed=AnalyzeFlowConstants(flow,CurrentLiveness(),values,expressions,plan_.primitives,tree_.texts,budget_.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
      facts.constants=std::get<FlowConstants>(std::move(analyzed));
      budget_.Admit(facts.constants->allocated_bytes,1);budget_.Step(facts.constants->work);
    }
    return *facts.constants;
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
    for(std::size_t i=0;i<uses_.size();++i){budget_.Step();const auto& use=uses_[i];
      if(use.private_storage&&use.declaration&&!use.escaped&&!use.outside_body&&values.bindings[i]&&
          values.bindings[i]->value!=i&&plan_.read_bindings[i]!=values.bindings[i])bindings[i]=values.bindings[i];}
    const auto& flow=CurrentFlow();
    std::vector<SourceNameQuery> queries;std::vector<SymbolId> owners;std::vector<std::uint8_t> blocked;
    budget_.Array(queries,flow.references.size());budget_.Array(owners,flow.references.size());budget_.Array(blocked,uses_.size());
    std::size_t query_count=0;
    for(const auto& reference:flow.references){budget_.Step();if(reference.kind!=FlowReferenceKind::kRead)continue;
      const auto target=bindings[reference.binding.value];if(!target)continue;
      if(!reference.owner||!flow.BindingInScope(*target,reference.owner)){blocked[reference.binding.value]=1;continue;}
      queries[query_count]={*target,reference.owner};owners[query_count++]=reference.binding;}
    queries.resize(query_count);owners.resize(query_count);
    const auto resolved=ResolveNames(flow,queries);
    for(std::size_t i=0;i<query_count;++i){budget_.Step();if(resolved[i]!=SourceNameResolution::kResolved)blocked[owners[i].value]=1;}
    // Read substitution only, after storage/snapshot destination ownership is
    // established. Keep every write/RHS/completion until independent cleanup.
    for(std::size_t i=0;i<bindings.size();++i){budget_.Step();if(bindings[i]&&!blocked[i]){BeginCommit();plan_.read_bindings[i]=bindings[i];}}
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
    uses_[symbol.value].escaped = true;
    // Creation is the earliest possible observation through this live cell.
    // Never infer read-only from the parent's lack of writes; child calls may
    // mutate it. Counting creation before RHS completion protects TDZ/throws.
    Observe(symbol, occurrence, container, projector);
  }
  void Captures(const PreparedFamily& prepared, ExprId occurrence,
      std::optional<StmtId> container, std::optional<StmtId> projector) {
    const auto& family = prepared.Family(); const auto& root = family.members[family.root.value].tree;
    for (const auto& alias : root.captures) {
      budget_.Step(); if (alias.target.owner == tree_.owner) Capture(alias.target.symbol, occurrence, container, projector);
    }
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
        if (const auto* created = std::get_if<FunctionExpr>(&node)) {
          for (auto symbol : std::span<const SymbolId>(tree_.closure_captures).subspan(created->captures.first, created->captures.count)) {
            budget_.Step(); Capture(symbol, visit.expression, container, projector);
          }
        }
        if (const auto* update = std::get_if<UpdateExpr>(&node)) {
          Observe(update->symbol, visit.expression, container, projector);
        }
        if (const auto* created = std::get_if<ClassExpr>(&node)) {
          const auto& definition = *created->definition;
          if (definition.constructor) Captures(*definition.constructor, visit.expression, container, projector);
          for (const auto& member : definition.members) { budget_.Step(); if (member) Captures(*member, visit.expression, container, projector); }
          for (const auto& site : definition.definitions) for (const auto& body : site.bodies) {
            budget_.Step(); if (body) Captures(*body, visit.expression, container, projector);
          }
          if (definition.initializer) for (const auto& alias : definition.initializer->tree.captures) {
            budget_.Step(); if (alias.target.owner == tree_.owner) Capture(alias.target.symbol, visit.expression, container, projector);
          }
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
  BindingInitializers plan_;
  std::vector<BindingUses> uses_;
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
  std::vector<std::optional<SourcePosition>> source_occurrences_;
  std::vector<TextId> literal_texts_;
  std::vector<std::optional<PrimitiveId>> expression_primitives_;
  PrimitiveId undefined_value_;
  std::vector<FlowValueEquation> value_equations_;
  std::vector<std::uint8_t> stable_entries_;
  bool mapped_arguments_=false;
  std::vector<std::optional<StmtId>> expression_owners_;
  std::vector<std::uint8_t> expression_projections_;
  std::vector<std::optional<SymbolId>> constant_heads_, constant_next_;
  std::vector<SymbolId> constant_queue_;
  std::vector<StatementVisit> statements_;
  RecoverySchedule schedule_;
  mutable std::optional<std::vector<std::uint8_t>> initialized_;
  mutable std::optional<ProjectionFacts> facts_;
  mutable RecoveryFlow flow_storage_{};
};
}
std::variant<BindingInitializers, TreeStructureError> PlanBindingInitializers(
    const FunctionTree& tree, const LoopSyntax& loops, const NullishSyntax& nullish, analysis::SemanticBudget budget) {
  try {
    TreeCheckBudget checked(budget); return Initializers(tree, loops, nullish, checked).Run();
  } catch (TreeStructureError error) { return error;
  } catch (const std::bad_alloc&) { return TreeStructureError::kResourceLimit;
  } catch (const std::length_error&) { return TreeStructureError::kResourceLimit; }
}
}
