#include "binding_initializer.h"
#include "binding_interference.h"
#include "definition_elision.h"
#include "flow_dominance.h"
#include "flow_definitions.h"
#include "class_tree.h"
#include "expression_children.h"
#include "loop_syntax.h"
#include "nullish_syntax.h"
#include "statement_children.h"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
struct BindingUses {
  std::optional<StmtId> declaration, initializer, initializer_container;
  std::optional<StmtId> definition, root_store;
  std::optional<ExprId> first;
  std::optional<ExprId> store;
  std::optional<std::size_t> first_container, last_container;
  std::size_t writes = 0;
  bool escaped = false, outside_body = false;
  bool private_storage = false, module_alias = false, named_creation = false;
};
struct ContainerRange { std::size_t begin = 0, end = 0; };
struct ObservationSite { std::size_t control = 0, region = 0, before = 0, after = 0; };
struct ProjectionObservation { SymbolId symbol; StmtId owner; };
struct ExpressionVisit {
  ExprId expression;
  bool finish = false, guarded = false;
  std::size_t parent_region = 0;
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
  bool guarded = false;
  std::size_t parent_region = 0;
};
class Initializers {
 public:
  Initializers(const FunctionTree& tree, const LoopSyntax& loops, const NullishSyntax& nullish,
      TreeCheckBudget& budget) : tree_(tree), loops_(loops), nullish_(nullish), budget_(budget) {
    budget.Array(plan_.statements, tree.statements.size()); budget.Array(plan_.omitted, tree.statements.size());
    budget.Array(plan_.loop_declarations, tree.statements.size());
    budget.Array(plan_.forwarded_statements, tree.statements.size());
    budget.Array(plan_.literals, tree.symbols.size());
    budget.Array(plan_.aliases, tree.symbols.size());
    budget.Array(plan_.storage, tree.symbols.size());
    budget.Array(plan_.undefined_stores, tree.statements.size());
    budget.Array(plan_.replacements, tree.expressions.size());
    budget.Array(completions_, tree.expressions.size());
    budget.Array(plan_.entry_counts, loops.initializer_roots.size() + 1);
    budget.Array(plan_.child_counts, tree.children.size() + 1);
    budget.Array(observations_, tree.expressions.size());
    budget.Array(expression_statements_, tree.expressions.size());
    budget.Array(container_parents_, tree.statements.size());
    budget.Array(container_order_, tree.statements.size());
    budget.Array(control_events_, tree.statements.size());
    budget.Array(constant_heads_, tree.symbols.size());budget.Array(constant_next_, tree.symbols.size());
    budget.Array(constant_queue_, tree.symbols.size());
    budget.Array(uses_, tree.symbols.size()); budget.Array(projected_, tree.statements.size());
    budget.Array(containers_, tree.statements.size());
    budget.Array(lexical_, tree.statements.size()); budget.Array(projectors_, tree.statements.size());
    budget.Array(retained_bodies_, tree.statements.size());
    budget.Array(projection_observations_, ProjectionCapacity());
    budget.Array(expressions_, tree.expressions.size()); budget.Array(statements_, tree.statements.size());
    budget.Array(expression_scopes_, tree.expressions.size());
    budget.Array(expression_owners_,tree.expressions.size());budget.Array(expression_projections_,tree.expressions.size());
    RequireTree(tree.statements.size()<std::numeric_limits<std::size_t>::max()-tree.expressions.size(),TreeStructureError::kResourceLimit);
    region_limit_=tree.statements.size()+tree.expressions.size()+1;
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
    for (auto symbol : tree_.closure_captures) { budget_.Step(); uses_[symbol.value].escaped = true; }
    for (const auto& alias : tree_.modules) {
      budget_.Step(); uses_[alias.symbol.value].escaped = true; uses_[alias.symbol.value].module_alias = true;
    }
    for (const auto& parameter : tree_.parameters) {
      budget_.Step(); if (parameter.initializer) Scan(*parameter.initializer, {}, {}, tree_.parameter_scope);
    }
    // Expression programs cannot host statement declarations. Loop entry may
    // instead use ForDeclaration after the same first-store/scope proof.
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
        visit.parent_region=current_region_;
        if(visit.guarded)current_region_=OpenRegion();
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
        }
        lexical_[visit.statement.value] = visit.lexical;
        Enter(visit);
      }
      const auto child = StatementChild(node, visit.child++, [&](const BlockStmt& block) {
        return std::span<const StmtId>(tree_.children).subspan(block.first_child, block.child_count);
      });
      if (!child) {
        if (std::holds_alternative<BlockStmt>(node) || loops_.initializers[visit.statement.value])
          containers_[visit.statement.value].end = next_container_;
        current_region_=visit.parent_region;
        --pending; continue;
      }
      const auto container = std::holds_alternative<BlockStmt>(node) ? std::optional{visit.statement} : std::nullopt;
      const auto projected = visit.projected; const auto lexical = visit.lexical; const auto projector = visit.projector;
      const auto scope=visit.scope;
      const bool guarded=std::holds_alternative<IfStmt>(node)||std::holds_alternative<LoopStmt>(node)||std::holds_alternative<TryStmt>(node);
      statements_[pending++] = {*child, container, lexical, projector, 0, projected};
      statements_[pending-1].scope=scope;
      statements_[pending-1].guarded=guarded;
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
    for (std::size_t i = 0; i < uses_.size(); ++i) {
      budget_.Step(); const auto& use = uses_[i];
      if (!use.declaration || !InitializerAvailable(use)) continue;
      if (use.escaped) {
        // A capture observes its cell from creation onward, even when invoked
        // only after this activation exits. Keep the original environment for
        // escaping locals: narrowing an outer cell into a loop body would give
        // each closure a different cell. Scope identity proves activation, not
        // a CFG block/adjacency/compiler-layout premise. Module readers can
        // observe the earlier undefined value and are not locally confined.
        if (use.module_alias || !use.initializer_container) continue;
        const auto* owner = std::get_if<BlockStmt>(&tree_.statements[use.initializer_container->value].statement);
        if (!owner || owner->scope != tree_.symbols[i].scope) continue;
      }
      const auto& effect = std::get<ExpressionStmt>(tree_.statements[use.initializer->value].statement);
      // DFS intervals prove that every observation stays in the new lexical
      // lifetime. A first completed direct store dominates later reads there,
      // including on each loop entry; RHS/earlier reads and sibling catch or
      // finally observations preserve the original undefined/old-value slot.
      // This is scope confinement, never a same-block admission. No SSA merge,
      // expression motion, source const inference or new capture environment.
      plan_.statements[use.initializer->value] = BindingInitializer{SymbolId{static_cast<std::uint32_t>(i)},
          effect.expression, use.private_storage && use.writes == 1 ? DeclarationKind::kConst : DeclarationKind::kLet};
      if (!loops_.initializer_owners[use.initializer->value]) plan_.omitted[use.declaration->value] = 1;
    }
    RunPass(Pass::kConstants);
    RunPass(Pass::kAliases);
    RunPass(Pass::kStoredResults);
    // Dead reads/stores must leave the projection before snapshot coverage is
    // analyzed. Forwarding preserves every RHS occurrence and removes its own
    // single read/store/declaration; it cannot make another definition dead.
    // Coalesce builds fresh ownership facts after that commit. Demand cleanup
    // is scheduled again only by later storage/declaration changes.
    RunPass(Pass::kDeadDefinitions);
    RunPass(Pass::kSnapshots);
    bool cleanup_needed=RunPass(Pass::kStorage);
    cleanup_needed |= RunPass(Pass::kStoredResults);
    cleanup_needed |= RunPass(Pass::kInertStatements);
    cleanup_needed |= RunPass(Pass::kLoopDeclarations);
    cleanup_needed |= RunPass(Pass::kStoredResults);
    cleanup_needed |= RunPass(Pass::kInertStatements);
    // Storage and loop declaration projection are now final. Their commits
    // invalidate facts before dependent demand/declaration cleanup.
    if(cleanup_needed)RunPass(Pass::kDeadDefinitions);
    for (std::size_t i = 0; i < loops_.initializer_roots.size(); ++i) {
      budget_.Step(); plan_.entry_counts[i + 1] = plan_.entry_counts[i] +
          !plan_.omitted[loops_.initializer_roots[i].statement.value];
    }
    AnalyzeExpressions();
    plan_.normal_stores = std::move(expression_facts_->normal_stores);
    plan_.evaluations = std::move(expression_facts_->evaluations);
    return Finish();
  }
 private:
  enum class Pass { kConstants,kAliases,kStoredResults,kDeadDefinitions,kSnapshots,
      kStorage,kInertStatements,kLoopDeclarations };
  // Contracts live here, not at individual scheduling sites. All changing
  // passes invalidate event-indexed facts. Dead/redundant write removal keeps
  // initialization of retained reads; storage merging proves the retained
  // root initialized at every remapped reference before commit. Only those
  // passes preserve positive occurrence facts. Missing facts remain unknown.
  bool RunPass(Pass pass) {
    bool changed=false,preserve=false;
    switch(pass) {
      case Pass::kConstants:changed=Constants();break;
      case Pass::kAliases:changed=Aliases();break;
      case Pass::kSnapshots:changed=ForwardSnapshots();break;
      case Pass::kLoopDeclarations:changed=ProjectLoopDeclarations();break;
      case Pass::kStoredResults:changed=ElideStoredResults();preserve=true;break;
      case Pass::kDeadDefinitions:changed=ElideDeadDefinitions();preserve=true;break;
      case Pass::kInertStatements:changed=OmitInertStores();preserve=true;break;
      case Pass::kStorage:changed=Coalesce();preserve=true;break;
    }
    if(changed) {
      if(flow_)flow_storage_=std::move(*flow_);
      liveness_.reset();flow_.reset();expression_facts_.reset();initializations_.reset();
      if(!preserve)initialized_.reset();
    }
    return changed;
  }
  bool ProjectLoopDeclarations() {
    bool changed=false;
    for (std::size_t i = 0; i < loops_.initializer_roots.size(); ++i) {
      budget_.Step(); plan_.entry_counts[i + 1] = plan_.entry_counts[i] +
          (plan_.omitted[loops_.initializer_roots[i].statement.value] ? 0 : 1);
    }
    // ForDeclaration has one ordered declarator list. Keep the preceding
    // expression program at its original entry, then emit the representable
    // suffix as let declarations. This is a grammar projection, not a compiler
    // layout premise: compound completion roots retain their expression form.
    // Every initializer root belongs to exactly one loop; no per-symbol walk.
    for (std::size_t i = 0; i < loops_.initializers.size(); ++i) {
      budget_.Step(); const auto& entry = loops_.initializers[i]; if (!entry) continue;
      auto first = entry->end;
      bool declaration = false;
      while (first > entry->first) {
        budget_.Step(); const auto root = loops_.initializer_roots[first - 1].statement;
        if (!plan_.statements[root.value] && !plan_.omitted[root.value]) break;
        declaration |= plan_.statements[root.value].has_value();
        --first;
      }
      if (declaration) plan_.loop_declarations[i] = first;
      for (auto at = entry->first; at < entry->end; ++at) {
        budget_.Step(); const auto root = loops_.initializer_roots[at].statement;
        if (at < first) {
          changed |= plan_.statements[root.value].has_value();
          plan_.statements[root.value].reset();
        }
        else if (!plan_.omitted[root.value]) {
          const auto symbol = plan_.statements[root.value]->symbol;
          changed |= plan_.statements[root.value]->declaration != DeclarationKind::kLet;
          plan_.statements[root.value]->declaration = DeclarationKind::kLet;
          changed |= !plan_.omitted[uses_[symbol.value].declaration->value];
          plan_.omitted[uses_[symbol.value].declaration->value] = 1;
        }
      }
    }
    return changed;
  }
  bool OmitInertStores() {
    const auto& facts = AnalyzeExpressions();
    const auto omitted = PlanInertStatements(facts);
    // The pass publishes a complete proposal before changing the projection.
    // No subsequent statement observes a partly applied omission batch.
    for (const auto statement : omitted) {
      budget_.Step(); plan_.omitted[statement.value] = 1;
    }
    return !omitted.empty();
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
  bool ForwardSnapshots() {
    const auto eligible=[&](std::size_t i) {
      const auto& use=uses_[i];
      return use.private_storage && use.declaration && use.writes==1 && use.store &&
          !use.escaped && use.root_store &&
          !plan_.omitted[use.root_store->value] && !plan_.aliases[i] && !plan_.literals[i];
    };
    bool candidate=false;
    for(std::size_t i=0;i<uses_.size();++i){budget_.Step();candidate |= eligible(i);}
    if(!candidate)return false;
    const auto coverage=AnalyzeBindingReadCoverage();
    std::vector<std::optional<ExprId>> reads;
    std::vector<std::size_t> counts, controls;
    std::vector<std::uint8_t> bound;
    std::vector<std::optional<StmtId>> scope_containers, environments;
    budget_.Array(reads, uses_.size()); budget_.Array(counts, uses_.size());
    for(const auto& reference:coverage.references) {
      budget_.Step();if(reference.kind!=FlowReferenceKind::kRead)continue;
      ++counts[reference.binding.value];
      reads[reference.binding.value]=std::get<ExprId>(reference.site);
    }
    budget_.Array(controls, control_ + 1);
    for (std::size_t i = 0; i < control_; ++i) {
      budget_.Step(); controls[i + 1] = controls[i] + !plan_.omitted[control_events_[i].value];
    }
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
      if (use.declaration && plan_.omitted[use.declaration->value]) continue;
      if (const auto owner = scope_containers[tree_.symbols[i].scope.value]) bound[owner->value] = 1;
    }
    for (std::size_t i = 0; i < plan_.statements.size(); ++i) {
      budget_.Step(); if (!plan_.statements[i]) continue;
      const auto owner = uses_[plan_.statements[i]->symbol.value].initializer_container;
      if (owner) bound[owner->value] = 1;
    }
    for (std::size_t i = 0; i < next_container_; ++i) {
      budget_.Step(); const auto id = container_order_[i]; const auto parent = container_parents_[id.value];
      environments[id.value] = bound[id.value] || !parent ? id : environments[parent->value];
    }
    // The census retains execution order independently of arena/block layout.
    // Reference counts above come from the current flow. This remaining legacy
    // census only bounds motion until shared effect/activation proofs replace it.
    // A read is a state observation even when GetBindingValue has no hook: an
    // effectful producer must not move past an earlier argument's value read.
    std::size_t epoch = 0;
    for (std::size_t i = 0; i < completion_count_; ++i) {
      budget_.Step(); const auto id = completions_[i]; auto& site = observations_[id.value];
      site.before = epoch;
      const auto owner = expression_statements_[id.value];
      if (owner && plan_.omitted[owner->value]) { site.after = epoch; continue; }
      const auto& node = tree_.expressions[id.value].expression;
      if (!plan_.replacements[id.value] && IntrinsicEvaluation(node) == ExpressionEvaluation::kRequired &&
          !(std::holds_alternative<SymbolExpr>(node) && plan_.literals[std::get<SymbolExpr>(node).symbol.value])) ++epoch;
      site.after = epoch;
    }
    struct ForwardedSnapshot { SymbolId binding; ExprId read, value; };
    std::vector<ForwardedSnapshot> proposed;
    budget_.Array(proposed,uses_.size());std::size_t count=0;
    for (std::size_t i = 0; i < uses_.size(); ++i) {
      budget_.Step(); const auto& use = uses_[i];
      if(!eligible(i) || counts[i]!=1 || !coverage.stores[i])continue;
      const auto read = *reads[i], store = *use.store;
      const auto& from = observations_[store.value]; const auto& to = observations_[read.value];
      if (from.after != to.before || controls[from.control] != controls[to.control] || from.region != to.region) continue;
      const auto old_owner = expression_owners_[store.value], new_owner = expression_owners_[read.value];
      if (!old_owner || !new_owner || environments[old_owner->value] != environments[new_owner->value]) continue;
      const auto rhs = std::get<AssignmentExpr>(tree_.expressions[store.value].expression).value;
      // Native naming follows the projected destination. Moving a direct
      // creation can require a computed-name wrapper; retain strict resource
      // contracts until no additional allocation is proved for that destination.
      if (ResourceFailures(tree_.identity.contract) != ResourceFailureScope::kExcluded &&
          (std::holds_alternative<FunctionExpr>(tree_.expressions[rhs.value].expression) ||
           std::holds_alternative<ClassExpr>(tree_.expressions[rhs.value].expression))) continue;
      proposed[count++]={SymbolId{static_cast<std::uint32_t>(i)},read,rhs};
    }
    // Value availability, motion and lexical proofs all observe the same
    // projection. Apply only after all proposals are complete; later passes
    // must reconstruct facts for the forwarded expression ownership.
    for(std::size_t i=0;i<count;++i) {
      budget_.Step();const auto& action=proposed[i];const auto& use=uses_[action.binding.value];
      plan_.replacements[action.read.value]=action.value;
      plan_.forwarded_statements[use.root_store->value]=1;
      plan_.omitted[use.declaration->value]=plan_.omitted[use.root_store->value]=1;
      plan_.statements[use.root_store->value].reset();
    }
    return count!=0;
  }
  const RecoveryFlow& CurrentFlow() const {
    if(flow_)return *flow_;
    // Bootstrap without initialization assumptions. Completion dominance on
    // this conservative graph proves facts before effect summaries are refined.
    // Unmodeled signature-program occurrences receive no body-entry proof.
    std::vector<std::uint8_t> private_bindings;
    budget_.Array(private_bindings,uses_.size());
    for (std::size_t i=0;i<uses_.size();++i) {
      budget_.Step();private_bindings[i]=tree_.symbols[i].kind==BindingKind::kCompilerTemporary && !uses_[i].escaped;
    }
    const auto build=[&](const auto& facts) {
      const LegacyFlowProjection projection{plan_.aliases,plan_.literals,plan_.replacements,
          plan_.omitted,facts.evaluations,facts.normal_stores,plan_.statements,private_bindings,plan_.storage,plan_.undefined_stores};
      auto built=BuildRecoveryFlow(tree_,projection,budget_.Remaining(),std::move(flow_storage_));
      if(const auto* error=std::get_if<TreeStructureError>(&built))throw *error;
      auto flow=std::get<RecoveryFlow>(std::move(built));
      budget_.Admit(flow.allocated_bytes,1);budget_.Step(flow.work);return flow;
    };
    if(initialized_) {
      if(!expression_facts_)expression_facts_=ComputeExpressions(*initialized_);
      flow_=build(*expression_facts_);return *flow_;
    }
    const auto conservative=ComputeExpressions({});
    auto provisional=build(conservative);
    auto analyzed=AnalyzeFlowInitializations(provisional,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    initializations_=std::get<FlowInitializations>(std::move(analyzed));
    const auto& initialization=*initializations_;
    budget_.Admit(initialization.allocated_bytes,1);budget_.Step(initialization.work);
    initialized_.emplace();auto& initialized=*initialized_;
    budget_.Array(initialized,tree_.expressions.size());
    for(const auto& reference:provisional.references) {
      budget_.Step();if(const auto* expression=std::get_if<ExprId>(&reference.site))
        initialized[expression->value]=initialization.Completed(reference.binding,reference.point);
    }
    expression_facts_=ComputeExpressions(initialized);
    auto refined=RefineRecoveryFlowEffects(provisional,tree_,expression_facts_->evaluations,
        expression_facts_->normal_stores,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&refined))throw *error;
    const auto& refinement=std::get<FlowRefinement>(refined);
    budget_.Admit(refinement.allocated_bytes,1);budget_.Step(refinement.work);
    flow_=std::move(provisional);return *flow_;
  }
  const FlowLiveness& CurrentLiveness() const {
    if(liveness_)return *liveness_;
    const auto& flow=CurrentFlow();
    auto analyzed=AnalyzeFlowLiveness(flow,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    liveness_=std::get<FlowLiveness>(std::move(analyzed));
    budget_.Admit(liveness_->allocated_bytes,1);budget_.Step(liveness_->work);return *liveness_;
  }
  bool ElideDeadDefinitions() {
    // Source naming recovery preserves NamedEvaluation after an assignment is
    // projected to its RHS, but can require a naming envelope. Its allocation
    // equivalence is not proved under the legacy resource-observable contract.
    // Retain that contract's representation without inspecting producer shapes.
    if (ResourceFailures(tree_.identity.contract)!=ResourceFailureScope::kExcluded) return false;
    bool candidate=false;
    for (std::size_t i=0;i<uses_.size();++i) {
      budget_.Step();candidate |= tree_.symbols[i].kind==BindingKind::kCompilerTemporary &&
          !uses_[i].escaped;
    }
    if (!candidate) return false;
    const auto& flow=CurrentFlow();
    auto analyzed=AnalyzeFlowDemand(flow,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    const auto& demand=std::get<FlowDemand>(analyzed);
    budget_.Admit(demand.allocated_bytes,1);budget_.Step(demand.work);
    auto proposed=PlanDefinitionElisions(flow,demand,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&proposed))throw *error;
    const auto& delta=std::get<DefinitionElisions>(proposed);
    budget_.Admit(delta.allocated_bytes,1);budget_.Step(delta.work);
    auto bindings=PlanBindingElisions(flow,delta,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&bindings))throw *error;
    const auto& binding_delta=std::get<BindingElisions>(bindings);
    budget_.Admit(binding_delta.allocated_bytes,1);budget_.Step(binding_delta.work);
    // Commit after demand closure. Only wholly inert discarded evaluations
    // disappear; every required RHS retains its occurrence and completion.
    // Binding removal consumes the proposed delta before either pass mutates
    // the projection. Storage analysis rebuilds after this batch commits.
    for (const auto statement:binding_delta.declarations) {budget_.Step();plan_.omitted[statement.value]=1;}
    for (const auto statement:delta.statements) {budget_.Step();plan_.omitted[statement.value]=1;}
    for (const auto write:delta.writes) {
      budget_.Step();plan_.replacements[write.value]=
          std::get<AssignmentExpr>(tree_.expressions[write.value].expression).value;
    }
    return !binding_delta.declarations.empty() || !delta.statements.empty() || !delta.writes.empty();
  }
  bool Coalesce() {
    const auto eligible = [&](SymbolId symbol) {
      const auto& use = uses_[symbol.value];
      return use.private_storage && use.declaration && !use.escaped && !use.outside_body &&
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
    if (!candidate) return false;
    const auto& flow=CurrentFlow();
    const auto& liveness=CurrentLiveness();
    auto analyzed = FindBindingInterference(flow,liveness,budget_.Remaining());
    if (const auto* error = std::get_if<TreeStructureError>(&analyzed)) throw *error;
    auto facts = std::get<BindingInterference>(std::move(analyzed));
    budget_.Admit(facts.allocated_bytes, 1); budget_.Step(facts.work);
    std::vector<std::optional<SymbolId>> proposed;
    std::vector<std::uint8_t> paired, blocked;
    budget_.Array(proposed, uses_.size()); budget_.Array(paired, uses_.size()); budget_.Array(blocked, uses_.size());
    // A disjoint matching avoids transitive interference: every accepted group
    // has the exact pair proved below, never an unchecked union of affinities.
    bool sharing=false;
    for (auto pair : facts.affinities) {
      budget_.Step(); if (pair.interference || paired[pair.first.value] || paired[pair.second.value] ||
          !eligible(pair.first) || !eligible(pair.second)) continue;
      const auto native = [&](SymbolId symbol) { const auto at = uses_[symbol.value].initializer;
        return at && plan_.statements[at->value].has_value(); };
      if (!native(pair.first) && native(pair.second)) std::swap(pair.first, pair.second);
      paired[pair.first.value] = paired[pair.second.value] = 1;
      proposed[pair.second.value] = pair.first;sharing=true;
    }
    if(!sharing)return false;
    // Effect refinement preserves positive dominance proofs; a rebuilt graph
    // after another pass needs fresh event-indexed proofs for this proposal.
    if(!initializations_) {
      auto analyzed=AnalyzeFlowInitializations(flow,budget_.Remaining());
      if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
      initializations_=std::get<FlowInitializations>(std::move(analyzed));
      budget_.Admit(initializations_->allocated_bytes,1);budget_.Step(initializations_->work);
    }
    const auto& initialization_order=*initializations_;
    std::vector<TreeScopedReference> references;
    std::vector<FlowReferenceSite> occurrences;
    std::vector<SymbolId> targets;
    budget_.Array(references,flow.references.size());budget_.Array(occurrences,flow.references.size());
    budget_.Array(targets,flow.references.size());std::size_t count=0;
    // Reference occurrences and execution order come from the same current
    // flow, including forwarded RHSs and update read/write halves. Original
    // arena order and expressions removed by earlier passes are not evidence.
    for(const auto& reference:flow.references) {
      budget_.Step();const auto root=proposed[reference.binding.value];if(!root)continue;
      if(!initialization_order.Completed(*root,reference.point))blocked[reference.binding.value]=1;
      const auto scope=std::visit([&](auto site) {
        if constexpr(std::is_same_v<decltype(site),ExprId>)return expression_scopes_[site.value];
        else return tree_.symbols[std::get<DeclarationStmt>(tree_.statements[site.value].statement).symbol.value].scope;
      },reference.site);
      references[count]={*root,scope};occurrences[count]=reference.site;targets[count++]=reference.binding;
    }
    references.resize(count); const auto visible = ResolveTreeReferences(tree_, references, budget_);
    for (std::size_t i = 0; i < count; ++i) {
      budget_.Step(); const auto root = references[i].symbol; const auto occurrence = occurrences[i];
      const auto lifetime=std::visit([&](auto site) {
        if constexpr(std::is_same_v<decltype(site),ExprId>)return RootLifetime(root,site);
        else return RootLifetime(root,lexical_[site.value]);
      },occurrence);
      if(!visible[i] || !lifetime)blocked[targets[i].value]=1;
    }
    for(std::size_t i=0;i<proposed.size();++i){budget_.Step();if(blocked[i])proposed[i].reset();}
    auto initialized=PlanStorageInitializations(flow,liveness,initialization_order,proposed,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&initialized))throw *error;
    const auto& initialization=std::get<StorageInitializations>(initialized);
    budget_.Admit(initialization.allocated_bytes,1);budget_.Step(initialization.work);
    // The independent initialization plan owns value preservation. Reuse the
    // source planner's lexical proof for its newly explicit write occurrences.
    std::vector<TreeScopedReference> initialization_references;
    std::vector<SymbolId> initialization_targets;
    budget_.Array(initialization_references,uses_.size());budget_.Array(initialization_targets,uses_.size());count=0;
    for(std::size_t i=0;i<proposed.size();++i) {
      budget_.Step();blocked[i] |= initialization.blocked[i];
      if(!proposed[i] || blocked[i] || !initialization.stores[uses_[i].declaration->value])continue;
      initialization_references[count]={*proposed[i],tree_.symbols[i].scope};
      initialization_targets[count++]={static_cast<std::uint32_t>(i)};
    }
    initialization_references.resize(count);initialization_targets.resize(count);
    if(count) {
      const auto resolved=ResolveTreeReferences(tree_,initialization_references,budget_);
      for(std::size_t i=0;i<count;++i) {
        budget_.Step();const auto target=initialization_targets[i];const auto root=*proposed[target.value];
        const auto declaration=*uses_[target.value].declaration;
        if(!resolved[i] || !RootLifetime(root,lexical_[declaration.value]))blocked[target.value]=1;
      }
    }
    bool changed=false;
    for (std::size_t i = 0; i < proposed.size(); ++i) {
      budget_.Step(); if (!proposed[i] || blocked[i]) continue;
      const auto root = *proposed[i]; const auto& use = uses_[i];
      plan_.storage[i] = root;changed=true;
      // Lifetime removal and value preservation are separate plan actions.
      if(initialization.stores[use.declaration->value])plan_.undefined_stores[use.declaration->value]=root;
      else plan_.omitted[use.declaration->value]=1;
      if (use.initializer) plan_.statements[use.initializer->value].reset();
      if (const auto initializer = uses_[root.value].initializer; initializer && plan_.statements[initializer->value])
        plan_.statements[initializer->value]->declaration = DeclarationKind::kLet;
    }
    return changed;
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
  bool ElideStoredResults() {
    // Analysis and proposals observe one immutable projection. Only this
    // commit changes replacements; consumers derive their own current facts.
    const auto& facts = AnalyzeExpressions();
    const auto replacements = PlanStoredResultElisions(facts);
    return ApplyStoredResultElisions(replacements);
  }
  struct ExpressionFacts {
    std::vector<std::optional<SymbolId>> bindings;
    std::vector<std::uint8_t> normal_stores;
    std::vector<ExpressionEvaluation> evaluations;
  };
  const ExpressionFacts& AnalyzeExpressions() const {
    if(!expression_facts_) {
      if(initialized_)expression_facts_=ComputeExpressions(*initialized_);
      else CurrentFlow();
    }
    return *expression_facts_;
  }
  ExpressionFacts ComputeExpressions(std::span<const std::uint8_t> initialized) const {
    ExpressionFacts facts;
    budget_.Array(facts.bindings, tree_.expressions.size());
    budget_.Array(facts.normal_stores, tree_.expressions.size());
    budget_.Array(facts.evaluations, tree_.expressions.size());
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
      auto& result = stored[id.value];
      const auto join = [&](ExprId left, ExprId right) -> std::optional<SymbolId> {
        return stored[left.value] == stored[right.value] ? stored[left.value] : std::nullopt;
      };
      if (const auto replacement = plan_.replacements[id.value]) {
        result = stored[replacement->value];
        facts.evaluations[id.value] = facts.evaluations[replacement->value];
        continue;
      }
      auto evaluation = IntrinsicEvaluation(node);
      if (const auto* read = std::get_if<SymbolExpr>(&node)) {
        if (!plan_.literals[read->symbol.value]) {
          const auto source = plan_.Storage(read->symbol);
          if (MutableBinding(source)) result = source;
        }
        if (plan_.literals[read->symbol.value] || (!initialized.empty() && initialized[id.value]))
          evaluation = ExpressionEvaluation::kInert;
      } else if (const auto* assignment = std::get_if<AssignmentExpr>(&node)) {
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
      VisitExpressionChildren(tree_, node, [&](ExprId child) {
        budget_.Step(); if (facts.evaluations[child.value] == ExpressionEvaluation::kRequired)
          evaluation = ExpressionEvaluation::kRequired;
      });
      facts.evaluations[id.value] = evaluation;
    }
    return facts;
  }
  std::vector<std::optional<ExprId>> PlanStoredResultElisions(const ExpressionFacts& facts) const {
    std::vector<std::optional<ExprId>> replacements;
    budget_.Array(replacements, tree_.expressions.size());
    for (std::size_t at = 0; at < completion_count_; ++at) {
      budget_.Step(); const auto id = completions_[at];
      if (plan_.replacements[id.value]) continue;
      const auto* assignment = std::get_if<AssignmentExpr>(&tree_.expressions[id.value].expression);
      // The result-binding fact is produced only for writable declarative
      // assignment targets. Missing identity is not evidence of equality.
      if (assignment && facts.bindings[id.value] &&
          facts.bindings[id.value] == facts.bindings[assignment->value.value])
        replacements[id.value] = assignment->value;
    }
    return replacements;
  }
  bool ApplyStoredResultElisions(const std::vector<std::optional<ExprId>>& replacements) {
    // A proposed outer elision uses the RHS normal-result identity, which is
    // preserved by every inner elision. Thus the batch does not depend on
    // proposal application order; no newly rewritten syntax is re-matched.
    bool changed=false;
    for (std::size_t i = 0; i < replacements.size(); ++i) {
      budget_.Step();
      if (replacements[i]) {
        plan_.replacements[i] = replacements[i];changed=true;
      }
    }
    return changed;
  }
  std::vector<StmtId> PlanInertStatements(const ExpressionFacts& facts) const {
    std::vector<StmtId> omitted;
    budget_.Array(omitted, tree_.statements.size()); std::size_t count = 0;
    // Only discard a projected store statement whose remaining evaluation is
    // inert. Value-observed occurrences and all required effects stay owned.
    for (std::size_t i = 0; i < tree_.statements.size(); ++i) {
      budget_.Step(); const auto* effect = std::get_if<ExpressionStmt>(&tree_.statements[i].statement);
      if (effect && plan_.replacements[effect->expression.value] &&
          facts.evaluations[effect->expression.value] == ExpressionEvaluation::kInert && !plan_.omitted[i])
        omitted[count++] = StmtId{static_cast<std::uint32_t>(i)};
    }
    omitted.resize(count); return omitted;
  }
  std::size_t OpenRegion() {
    RequireTree(next_region_<region_limit_,TreeStructureError::kOwnership);
    return next_region_++;
  }
  // Transitional for-let lifetime proof; SourceVariables will consume the
  // shared binding activation/initialization facts, not another scope census.
  bool RootLifetime(SymbolId root,ExprId occurrence) const {
    return RootLifetime(root,expression_owners_[occurrence.value]);
  }
  bool RootLifetime(SymbolId root,std::optional<StmtId> owner) const {
    const auto& use=uses_[root.value];
    if(!use.initializer||!plan_.statements[use.initializer->value])return true;
    // A prospective native declaration may narrow its original lifetime (for
    // let includes test/body/update). Availability must cover substituted reads
    // in the printed owner, not only the canonical symbol's original scope.
    if(!owner||!use.initializer_container)return false;
    const auto at=containers_[owner->value].begin;const auto range=containers_[use.initializer_container->value];
    return at>=range.begin&&at<range.end;
  }
  void Elide(const BindingUses& use) {
    plan_.omitted[use.declaration->value]=1;
    const auto rhs=std::get<AssignmentExpr>(tree_.expressions[use.store->value].expression).value;
    if(use.initializer) {
      plan_.omitted[use.initializer->value]=1;plan_.statements[use.initializer->value].reset();
    } else plan_.replacements[use.store->value]=rhs;
  }
  bool Confined(const BindingUses& use, std::optional<StmtId> owner) const {
    if (use.outside_body || !owner || !use.first_container) return false;
    const auto range = containers_[owner->value];
    return *use.first_container >= range.begin && *use.last_container < range.end;
  }
  bool InitializerAvailable(const BindingUses& use) const {
    if (!use.initializer) return false;
    const auto& effect=std::get<ExpressionStmt>(tree_.statements[use.initializer->value].statement);
    return use.first==effect.expression && Confined(use,use.initializer_container);
  }
  struct BindingReadCoverage {
    std::vector<std::uint8_t> stores, declarations;
    std::span<const FlowReference> references;
  };
  BindingReadCoverage AnalyzeBindingReadCoverage() const {
    const auto& flow=CurrentFlow();
    const auto& live=CurrentLiveness();
    auto analyzed=AnalyzeFlowReadCoverage(flow,live,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
    const auto& coverage=std::get<FlowReadCoverage>(analyzed);
    budget_.Admit(coverage.allocated_bytes,1);budget_.Step(coverage.work);
    BindingReadCoverage result;
    budget_.Array(result.stores,uses_.size());budget_.Array(result.declarations,uses_.size());
    for(std::size_t i=0;i<flow.definitions.size();++i) {
      budget_.Step();const auto& definition=flow.definitions[i];const auto& use=uses_[definition.binding.value];
      if(definition.write && definition.write==use.store)
        result.stores[definition.binding.value]=coverage.definitions[i];
      if(definition.declaration && definition.declaration==use.definition)
        result.declarations[definition.binding.value]=coverage.definitions[i];
    }
    result.references=flow.references;
    return result;
  }
  bool Constants() {
    const auto eligible=[&](const BindingUses& use) {
      return use.private_storage && use.declaration && use.writes==1 && use.store && !use.escaped;
    };
    bool seed=false;
    for(const auto& use:uses_) {
      budget_.Step();if(!eligible(use))continue;
      const auto value=std::get<AssignmentExpr>(tree_.expressions[use.store->value].expression).value;
      seed |= std::holds_alternative<TreeLiteral>(tree_.expressions[value.value].expression);
    }
    if(!seed)return false;
    const auto coverage=AnalyzeBindingReadCoverage();
    std::vector<std::optional<ExprId>> literals;
    budget_.Array(literals,uses_.size());
    // A primitive literal has the same value on every execution of its store.
    // Shared coverage excludes reads of undefined or any other definition;
    // unlike effectful copies this needs no dynamic occurrence identity.
    // Propagate proposals once per dependency edge, then commit as a batch.
    std::size_t ready = 0;
    for (std::size_t i = 0; i < uses_.size(); ++i) {
      budget_.Step();const auto& use = uses_[i];
      if (!eligible(use) || !coverage.stores[i]) continue;
      const auto symbol = SymbolId{static_cast<std::uint32_t>(i)};
      const auto assignment = *use.store;
      const auto value = std::get<AssignmentExpr>(tree_.expressions[assignment.value].expression).value;
      const auto& expression = tree_.expressions[value.value].expression;
      if (std::holds_alternative<TreeLiteral>(expression)) {
        literals[i] = value;constant_queue_[ready++] = symbol;
      } else if (const auto* read = std::get_if<SymbolExpr>(&expression)) {
        constant_next_[i] = constant_heads_[read->symbol.value];constant_heads_[read->symbol.value] = symbol;
      }
    }
    for (std::size_t at = 0; at < ready; ++at) {
      budget_.Step();const auto source = constant_queue_[at];
      for (auto target = constant_heads_[source.value]; target; target = constant_next_[target->value]) {
        budget_.Step();RequireTree(!literals[target->value] && ready < constant_queue_.size(),TreeStructureError::kOwnership);
        literals[target->value] = literals[source.value];constant_queue_[ready++] = *target;
      }
    }
    for (std::size_t i = 0; i < uses_.size(); ++i) {
      budget_.Step();if (!literals[i]) continue;
      plan_.literals[i]=literals[i];
      Elide(uses_[i]);
    }
    return ready!=0;
  }
  bool Aliases() {
    // A single retained definition must cover all source reads, including each
    // copy RHS; each copy definition must in turn cover its destination reads.
    // A source re-execution reaching a destination read without re-executing
    // its copy would also admit a path bypassing that copy from function entry,
    // contradicting coverage. This is the SSA copy-propagation obligation;
    // name resolution, escaping cells and activation remain separate below.
    bool candidate=false;
    for(std::size_t i=0;i<uses_.size();++i) {
      budget_.Step();const auto& use=uses_[i];
      if(!use.private_storage || !use.declaration || use.writes!=1 || !use.store || use.escaped || plan_.literals[i])continue;
      const auto rhs=std::get<AssignmentExpr>(tree_.expressions[use.store->value].expression).value;
      candidate |= std::holds_alternative<SymbolExpr>(tree_.expressions[rhs.value].expression);
    }
    if(!candidate)return false;
    // RunPass invalidates facts if Constants committed a change. A no-op may
    // share its immutable flow/liveness; coverage always uses the current plan.
    const auto coverage=AnalyzeBindingReadCoverage();
    std::vector<std::optional<SymbolId>> roots;
    budget_.Array(roots,uses_.size());std::size_t ready=0;
    // ECMA-262 2024 FunctionDeclarationInstantiation: only a non-strict,
    // non-arrow simple parameter list may couple formals to mapped arguments.
    // Scope activation initializes catch/self and all body-entry parameters;
    // local/module/capture absence of writes is not an initialization proof.
    bool simple=true;
    for(const auto& parameter:tree_.parameters) {
      budget_.Step();simple &= !parameter.initializer && !parameter.rest;
    }
    const bool mapped=tree_.strictness==Strictness::kSloppy&&!IsArrowCallable(tree_.callable)&&simple;
    const auto implicit=[&](BindingKind kind) {
      switch(kind) {
        case BindingKind::kParameter:return !mapped;
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
      if(!use.writes&&!use.escaped&&implicit(tree_.symbols[i].kind)) {
        roots[i]=symbol;constant_queue_[ready++]=symbol;continue;
      }
      // Declaration completion is another initialization event, not an opcode
      // or producer shape. Retain its original value/effects/NamedEvaluation;
      // only later private copies disappear. The same observation lifetime
      // prevents stale copies across activation/TDZ/catch/loop boundaries.
      if(use.definition&&!use.writes&&!use.escaped&&coverage.declarations[i]&&
          Confined(use,lexical_[use.definition->value])) {
        roots[i]=symbol;constant_queue_[ready++]=symbol;continue;
      }
      if(use.writes!=1||use.escaped||!coverage.stores[i]||plan_.literals[i])continue;
      if(!use.private_storage || !use.declaration) {
        // A retained source/cell first store uses the identical availability
        // proof. A deleted constant store cannot provide retained storage.
        roots[i]=symbol;constant_queue_[ready++]=symbol;
        continue;
      }
      const auto assignment=*use.store;
      const auto rhs=std::get<AssignmentExpr>(tree_.expressions[assignment.value].expression).value;
      if(const auto* copy=std::get_if<SymbolExpr>(&tree_.expressions[rhs.value].expression)) {
        constant_next_[i]=constant_heads_[copy->symbol.value];constant_heads_[copy->symbol.value]=symbol;
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
    if(!candidates)return false;
    std::vector<std::optional<SymbolId>> copy_targets,blocked_sources;
    std::vector<std::uint8_t> blocked;
    std::vector<TreeScopedReference> references;
    struct AliasRead { ExprId expression; SymbolId binding; };
    std::vector<AliasRead> occurrences;
    budget_.Array(copy_targets,tree_.expressions.size());budget_.Array(blocked_sources,uses_.size());
    budget_.Array(blocked,uses_.size());budget_.Array(references,coverage.references.size());
    budget_.Array(occurrences,coverage.references.size());
    for(std::size_t i=0;i<roots.size();++i) {
      budget_.Step();if(!roots[i]||roots[i]->value==i)continue;
      // A statement store disappears entirely. An embedded store still yields
      // its RHS at that occurrence; lexical resolution remains required there.
      if(!uses_[i].initializer)continue;
      const auto assignment=*uses_[i].store;
      copy_targets[std::get<AssignmentExpr>(tree_.expressions[assignment.value].expression).value.value]=SymbolId{static_cast<std::uint32_t>(i)};
    }
    std::size_t count=0;
    // Name/lifetime obligations belong to retained semantic reads, including
    // compound operations. Their binding identity comes from the same flow as
    // coverage, never a second census of original SymbolExpr nodes.
    for(const auto& read:coverage.references) {
      budget_.Step();if(read.kind!=FlowReferenceKind::kRead)continue;
      if(!roots[read.binding.value]||roots[read.binding.value]==read.binding)continue;
      const auto expression=std::get<ExprId>(read.site);
      references[count]={*roots[read.binding.value],expression_scopes_[expression.value]};
      occurrences[count++]={expression,read.binding};
    }
    references.resize(count);const auto visible=ResolveTreeReferences(tree_,references,budget_);
    ready=0;
    const auto retain=[&](SymbolId symbol) {
      if(!blocked[symbol.value]) {
        RequireTree(ready<constant_queue_.size(),TreeStructureError::kOwnership);
        blocked[symbol.value]=1;constant_queue_[ready++]=symbol;
      }
    };
    for(std::size_t i=0;i<count;++i) {
      budget_.Step();if(visible[i]&&RootLifetime(references[i].symbol,occurrences[i].expression))continue;
      const auto occurrence=occurrences[i].expression;const auto source=occurrences[i].binding;
      // A copy RHS ceases to be a read when its store is elided. Only if the
      // destination must remain does its unavailable root force source storage.
      if(const auto target=copy_targets[occurrence.value])blocked_sources[target->value]=source;
      else retain(source);
    }
    for(std::size_t at=0;at<ready;++at) {
      budget_.Step();if(const auto source=blocked_sources[constant_queue_[at].value])retain(*source);
    }
    bool changed=false;
    for(std::size_t i=0;i<roots.size();++i) {
      budget_.Step();if(!roots[i]||roots[i]->value==i||blocked[i])continue;
      plan_.aliases[i]=roots[i];Elide(uses_[i]);changed=true;
    }
    return changed;
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
      bool write, std::optional<StmtId> projector) {
    auto& use = uses_[symbol.value]; if (!use.first) use.first = expression;
    if (projector) {
      RequireTree(projection_count_ < projection_observations_.size(), TreeStructureError::kOwnership);
      projection_observations_[projection_count_++] = {symbol, *projector};
    }
    else ObserveContainer(use, container);
    if (write) ++use.writes;
  }
  void Capture(SymbolId symbol, ExprId occurrence, std::optional<StmtId> container,
      std::optional<StmtId> projector) {
    uses_[symbol.value].escaped = true;
    // Creation is the earliest possible observation through this live cell.
    // Never infer read-only from the parent's lack of writes; child calls may
    // mutate it. Counting creation before RHS completion protects TDZ/throws.
    Observe(symbol, occurrence, container, false, projector);
  }
  void Captures(const PreparedFamily& prepared, ExprId occurrence,
      std::optional<StmtId> container, std::optional<StmtId> projector) {
    const auto& family = prepared.Family(); const auto& root = family.members[family.root.value].tree;
    for (const auto& alias : root.captures) {
      budget_.Step(); if (alias.target.owner == tree_.owner) Capture(alias.target.symbol, occurrence, container, projector);
    }
  }
  void Scan(ExprId root, std::optional<StmtId> container, std::optional<StmtId> projector, ScopeId scope) {
    std::size_t pending = 1; expressions_[0] = {root};
    while (pending) {
      budget_.Step(); const auto visit = expressions_[--pending];
      const auto& node = tree_.expressions[visit.expression.value].expression;
      if (!visit.finish) {
        const auto parent_region=current_region_;
        if(visit.guarded)current_region_=OpenRegion();
        expression_scopes_[visit.expression.value]=scope;
        expression_statements_[visit.expression.value] = current_statement_;
        observations_[visit.expression.value].control = control_;
        observations_[visit.expression.value].region = current_region_;
        expression_owners_[visit.expression.value]=projector?projector:container;
        expression_projections_[visit.expression.value]=projector.has_value();
        expressions_[pending++] = {visit.expression, true,visit.guarded,parent_region};
        const auto first_child=pending;
        VisitExpressionChildren(tree_, node, [&](ExprId child) {
          budget_.Step(); RequireTree(pending < expressions_.size(), TreeStructureError::kOwnership);
          const auto* logical=std::get_if<LogicalExpr>(&node);
          const auto* conditional=std::get_if<ConditionalExpr>(&node);
          const bool guarded=(logical&&child==logical->right)||(conditional&&child!=conditional->condition);
          expressions_[pending++] = {child,false,guarded};
        });
        // VisitExpressionChildren owns source evaluation order. A LIFO worklist
        // must reverse its pushed children, never evaluate arguments backwards.
        // Short-circuit/choice arms have distinct execution regions; a completed
        // store cannot initialize observations outside its activation. RHS
        // completion precedes identifier PutValue, including await/yield failure.
        std::reverse(expressions_.begin()+first_child,expressions_.begin()+pending);
        if (const auto* read = std::get_if<SymbolExpr>(&node)) Observe(read->symbol, visit.expression, container, false, projector);
        if (const auto* created = std::get_if<FunctionExpr>(&node)) {
          for (auto symbol : std::span<const SymbolId>(tree_.closure_captures).subspan(created->captures.first, created->captures.count)) {
            budget_.Step(); Capture(symbol, visit.expression, container, projector);
          }
        }
        if (const auto* update = std::get_if<UpdateExpr>(&node)) {
          Observe(update->symbol, visit.expression, container, true, projector);
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
            Observe(target->symbol, visit.expression, container, true, projector);
            auto& use=uses_[target->symbol.value];
            const auto& rhs = tree_.expressions[assignment->value.value].expression;
            use.named_creation |= std::holds_alternative<FunctionExpr>(rhs) || std::holds_alternative<ClassExpr>(rhs);
            if(use.definition&&!use.store)use.store=visit.expression;
          }
        }
        current_region_=visit.parent_region;
      }
    }
  }
  void Enter(const StatementVisit& visit) {
    current_statement_ = visit.statement;
    const auto& node = tree_.statements[visit.statement.value].statement;
    std::visit([&](const auto& syntax) {
      using T = std::decay_t<decltype(syntax)>;
      if constexpr (std::is_same_v<T, DeclarationStmt>) {
        if (syntax.initializer) Scan(*syntax.initializer, visit.lexical, visit.projector,visit.scope);
        const auto& symbol = tree_.symbols[syntax.symbol.value]; auto& use = uses_[syntax.symbol.value];
        use.definition=visit.statement;
        if (!syntax.initializer && (symbol.kind == BindingKind::kCompilerTemporary || symbol.kind == BindingKind::kLocal) &&
            symbol.declaration == DeclarationKind::kLet && visit.container && !visit.projected) {
          use.declaration = visit.statement;
          use.private_storage = symbol.kind == BindingKind::kCompilerTemporary;
        }
      } else if constexpr (std::is_same_v<T, ExpressionStmt>) {
        Scan(syntax.expression, visit.lexical, visit.projector,visit.scope);
        if (const auto* assignment = std::get_if<AssignmentExpr>(&tree_.expressions[syntax.expression.value].expression))
          if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) {
            auto& use = uses_[target->symbol.value];
            if (use.store == syntax.expression) use.root_store = visit.statement;
            const auto entry = loops_.initializer_owners[visit.statement.value];
            if (use.definition && ((!visit.projected && visit.container) || entry) && !use.initializer) {
              use.initializer = visit.statement; use.initializer_container = entry ? entry : visit.container;
            }
          }
      } else if constexpr (std::is_same_v<T, IfStmt>) Scan(syntax.condition, visit.lexical, visit.projector,visit.scope);
      else if constexpr (std::is_same_v<T, ReturnStmt>) { if (syntax.value) Scan(*syntax.value, visit.lexical, visit.projector,visit.scope); }
      else if constexpr (std::is_same_v<T, ThrowStmt>) Scan(syntax.value, visit.lexical, visit.projector,visit.scope);
      else static_assert(std::is_same_v<T, BlockStmt> || std::is_same_v<T, TryStmt> || std::is_same_v<T, LoopStmt> ||
          std::is_same_v<T, BreakStmt> || std::is_same_v<T, ContinueStmt> || std::is_same_v<T, DebuggerStmt> || std::is_same_v<T, UnrecoveredStmt>);
    }, node);
    // A condition is evaluated before its control split; a return operand before
    // its transfer. Subsequent statements cannot forward effects across either.
    if (!std::holds_alternative<BlockStmt>(node) && !std::holds_alternative<ExpressionStmt>(node))
      control_events_[control_++] = visit.statement;
  }
  const FunctionTree& tree_;
  const LoopSyntax& loops_;
  const NullishSyntax& nullish_;
  TreeCheckBudget& budget_;
  BindingInitializers plan_;
  std::vector<BindingUses> uses_;
  std::vector<std::uint8_t> projected_;
  std::vector<ContainerRange> containers_;
  std::vector<ObservationSite> observations_;
  std::vector<std::optional<StmtId>> expression_statements_, container_parents_;
  std::vector<StmtId> container_order_, control_events_;
  std::optional<StmtId> current_statement_;
  std::size_t control_ = 0;
  std::vector<std::optional<StmtId>> lexical_, projectors_;
  std::vector<std::optional<StmtId>> retained_bodies_;
  std::vector<ProjectionObservation> projection_observations_;
  std::size_t projection_count_ = 0;
  std::size_t next_container_ = 0;
  std::vector<ExpressionVisit> expressions_;
  std::vector<ExprId> completions_;
  std::size_t completion_count_ = 0;
  std::vector<ScopeId> expression_scopes_;
  std::vector<std::optional<StmtId>> expression_owners_;
  std::vector<std::uint8_t> expression_projections_;
  std::size_t region_limit_=0;
  std::size_t current_region_=0,next_region_=1;
  std::vector<std::optional<SymbolId>> constant_heads_, constant_next_;
  std::vector<SymbolId> constant_queue_;
  std::vector<StatementVisit> statements_;
  mutable std::optional<std::vector<std::uint8_t>> initialized_;
  mutable std::optional<FlowInitializations> initializations_;
  mutable std::optional<ExpressionFacts> expression_facts_;
  mutable RecoveryFlow flow_storage_{};
  mutable std::optional<RecoveryFlow> flow_;
  mutable std::optional<FlowLiveness> liveness_;
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
