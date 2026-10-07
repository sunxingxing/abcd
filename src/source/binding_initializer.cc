#include "binding_initializer.h"
#include "binding_interference.h"
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
  std::size_t definition_region = 0;
  std::optional<ExprId> first;
  std::optional<ExprId> store;
  std::size_t store_region = 0;
  std::optional<std::size_t> first_region, last_region;
  std::optional<std::size_t> first_container, last_container;
  std::size_t writes = 0;
  bool escaped = false, outside_body = false, initialized_first = false;
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
    budget.Array(declaration_controls_, tree.symbols.size());
    budget.Array(completion_order_, tree.expressions.size());
    budget.Array(plan_.replacements, tree.expressions.size());
    budget.Array(plan_.evaluations, tree.expressions.size());
    budget.Array(plan_.normal_stores, tree.expressions.size());
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
    budget.Array(regions_,tree.statements.size()+tree.expressions.size()+1);
    regions_[0].begin=0;
  }
  BindingInitializers Run() {
    if (!tree_.recovery_issues.empty()) return Finish();
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
        if(visit.guarded)regions_[current_region_].end=next_region_;
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
    regions_[0].end=next_region_;
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
    Constants();
    Aliases();
    RefreshProjection();
    ForwardSnapshots();
    Coalesce();
    RefreshProjection();
    OmitInertStores();
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
        if (at < first) plan_.statements[root.value].reset();
        else if (!plan_.omitted[root.value]) {
          const auto symbol = plan_.statements[root.value]->symbol;
          plan_.statements[root.value]->declaration = DeclarationKind::kLet;
          plan_.omitted[uses_[symbol.value].declaration->value] = 1;
        }
      }
    }
    RefreshProjection();
    OmitInertStores();
    for (std::size_t i = 0; i < loops_.initializer_roots.size(); ++i) {
      budget_.Step(); plan_.entry_counts[i + 1] = plan_.entry_counts[i] +
          !plan_.omitted[loops_.initializer_roots[i].statement.value];
    }
    return Finish();
  }
 private:
  void OmitInertStores() {
    // A projected store whose remaining result is inert needs no discarded
    // statement. Keep value-observed occurrences and every remaining effect.
    for (std::size_t i = 0; i < tree_.statements.size(); ++i) {
      budget_.Step(); const auto* effect = std::get_if<ExpressionStmt>(&tree_.statements[i].statement);
      if (effect && plan_.replacements[effect->expression.value] &&
          plan_.evaluations[effect->expression.value] == ExpressionEvaluation::kInert) plan_.omitted[i] = 1;
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
  // Legacy initialization owner until canonical RecoveryFlow definitions replace
  // this census. New recovery passes must not grow separate TDZ predicates.
  bool InitializedRead(SymbolId symbol, ExprId occurrence) const {
    if (plan_.aliases[symbol.value] || plan_.literals[symbol.value]) return true;
    const auto kind = tree_.symbols[symbol.value].kind;
    // A parameter read in its own/default parameter program can still be TDZ.
    // Invocation bindings are initialized on body entry; reads need no value
    // immutability proof, since mutation does not make GetBindingValue a hook.
    if (kind == BindingKind::kFunctionSelf || kind == BindingKind::kCatchParameter) return true;
    if (kind == BindingKind::kParameter) return expression_owners_[occurrence.value].has_value();
    const auto& use = uses_[symbol.value];
    if (!use.definition || !use.initialized_first ||
        !Confined(use, lexical_[use.definition->value]) || !use.first_region) return false;
    const auto region = regions_[use.definition_region];
    return *use.first_region >= region.begin && *use.last_region < region.end;
  }
  void ForwardSnapshots() {
    std::vector<std::optional<ExprId>> reads;
    std::vector<std::size_t> counts, controls;
    std::vector<std::uint8_t> bound;
    std::vector<std::optional<StmtId>> scope_containers, environments;
    budget_.Array(reads, uses_.size()); budget_.Array(counts, uses_.size());
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
    // Count actual retained binding observations after copy/literal projection.
    // A read is a state observation even when GetBindingValue has no hook: an
    // effectful producer must not move past an earlier argument's value read.
    std::size_t epoch = 0;
    for (std::size_t i = 0; i < completion_count_; ++i) {
      budget_.Step(); const auto id = completions_[i]; auto& site = observations_[id.value];
      site.before = epoch;
      const auto owner = expression_statements_[id.value];
      if (owner && plan_.omitted[owner->value]) { site.after = epoch; continue; }
      const auto& node = tree_.expressions[id.value].expression;
      if (const auto* read = std::get_if<SymbolExpr>(&node); read && !plan_.literals[read->symbol.value]) {
        const auto symbol = plan_.aliases[read->symbol.value].value_or(read->symbol);
        ++counts[symbol.value]; reads[symbol.value] = id;
      }
      if (!plan_.replacements[id.value] && IntrinsicEvaluation(node) == ExpressionEvaluation::kRequired &&
          !(std::holds_alternative<SymbolExpr>(node) && plan_.literals[std::get<SymbolExpr>(node).symbol.value])) ++epoch;
      site.after = epoch;
    }
    for (std::size_t i = 0; i < uses_.size(); ++i) {
      budget_.Step(); const auto& use = uses_[i];
      if (!PrivateCopy(use) || !use.root_store || counts[i] != 1 ||
          plan_.omitted[use.root_store->value] || plan_.aliases[i] || plan_.literals[i]) continue;
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
      plan_.replacements[read.value] = rhs;
      plan_.forwarded_statements[use.root_store->value] = 1;
      plan_.omitted[use.declaration->value] = plan_.omitted[use.root_store->value] = 1;
      plan_.statements[use.root_store->value].reset();
    }
  }
  void Coalesce() {
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
    if (!candidate) return;
    const LegacyFlowProjection projection{plan_.aliases,plan_.literals,plan_.replacements,
        plan_.omitted,plan_.evaluations,plan_.normal_stores};
    auto built=BuildRecoveryFlow(tree_,projection,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&built))throw *error;
    auto flow=std::get<RecoveryFlow>(std::move(built));
    budget_.Admit(flow.allocated_bytes,1);budget_.Step(flow.work);
    auto live=AnalyzeFlowLiveness(flow,budget_.Remaining());
    if(const auto* error=std::get_if<TreeStructureError>(&live))throw *error;
    const auto& liveness=std::get<FlowLiveness>(live);
    budget_.Admit(liveness.allocated_bytes,1);budget_.Step(liveness.work);
    auto analyzed = FindBindingInterference(flow,liveness,budget_.Remaining());
    if (const auto* error = std::get_if<TreeStructureError>(&analyzed)) throw *error;
    auto facts = std::get<BindingInterference>(std::move(analyzed));
    budget_.Admit(facts.allocated_bytes, 1); budget_.Step(facts.work);
    std::vector<std::optional<SymbolId>> proposed;
    std::vector<std::uint8_t> paired, blocked;
    budget_.Array(proposed, uses_.size()); budget_.Array(paired, uses_.size()); budget_.Array(blocked, uses_.size());
    // A disjoint matching avoids transitive interference: every accepted group
    // has the exact pair proved below, never an unchecked union of affinities.
    for (auto pair : facts.affinities) {
      budget_.Step(); if (pair.interference || paired[pair.first.value] || paired[pair.second.value] ||
          !eligible(pair.first) || !eligible(pair.second)) continue;
      const auto native = [&](SymbolId symbol) { const auto at = uses_[symbol.value].initializer;
        return at && plan_.statements[at->value].has_value(); };
      if (!native(pair.first) && native(pair.second)) std::swap(pair.first, pair.second);
      // Removing the empty declaration must also remove only a dead initial
      // undefined value. Transitional obligation: DefinitionElision must own
      // keep/delete decisions, including a live undefined initialization after
      // storage sharing. Do not expand CompletedStore; see unified-variables.md.
      if (!CompletedStore(uses_[pair.second.value])) continue;
      paired[pair.first.value] = paired[pair.second.value] = 1;
      proposed[pair.second.value] = pair.first;
    }
    std::vector<TreeScopedReference> references;
    std::vector<ExprId> occurrences;
    std::vector<SymbolId> targets;
    budget_.Array(references, tree_.expressions.size()); budget_.Array(occurrences, tree_.expressions.size());
    budget_.Array(targets, tree_.expressions.size()); std::size_t count = 0;
    for (std::size_t i = 0; i < tree_.expressions.size(); ++i) {
      budget_.Step(); const auto& expression = tree_.expressions[i].expression;
      std::optional<SymbolId> symbol;
      if (const auto* read = std::get_if<SymbolExpr>(&expression)) symbol = plan_.aliases[read->symbol.value].value_or(read->symbol);
      else if (const auto* update = std::get_if<UpdateExpr>(&expression)) symbol = update->symbol;
      else if (const auto* assignment = std::get_if<AssignmentExpr>(&expression))
        if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) symbol = target->symbol;
      if (!symbol || !proposed[symbol->value]) continue;
      references[count] = {*proposed[symbol->value], expression_scopes_[i]};
      occurrences[count] = {static_cast<std::uint32_t>(i)}; targets[count++] = *symbol;
    }
    references.resize(count); const auto visible = ResolveTreeReferences(tree_, references, budget_);
    for (std::size_t i = 0; i < count; ++i) {
      budget_.Step(); const auto root = references[i].symbol; const auto occurrence = occurrences[i];
      const auto& use = uses_[root.value]; const auto& site = observations_[occurrence.value];
      const auto region = regions_[use.definition_region];
      const bool initialized = site.control > declaration_controls_[root.value] &&
          site.region >= region.begin && site.region < region.end;
      const bool native_ready = !use.initializer || !plan_.statements[use.initializer->value] ||
          (use.store && completion_order_[use.store->value] < completion_order_[occurrence.value]);
      if (!visible[i] || !RootLifetime(root, occurrence) || !initialized || !native_ready) blocked[targets[i].value] = 1;
    }
    for (std::size_t i = 0; i < proposed.size(); ++i) {
      budget_.Step(); if (!proposed[i] || blocked[i]) continue;
      const auto root = *proposed[i]; const auto& use = uses_[i];
      plan_.storage[i] = root; plan_.omitted[use.declaration->value] = 1;
      if (use.initializer) plan_.statements[use.initializer->value].reset();
      if (const auto initializer = uses_[root.value].initializer; initializer && plan_.statements[initializer->value])
        plan_.statements[initializer->value]->declaration = DeclarationKind::kLet;
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
  void RefreshProjection() {
    // Dependency boundary: propose against one immutable projection, commit
    // all elisions, then compute effects for the resulting projection. No
    // consumer may observe effects from before the replacement commit.
    ApplyStoredResultElisions();
    AnalyzeEvaluations();
  }
  void ApplyStoredResultElisions() {
    std::vector<std::optional<ExprId>> replacements;
    budget_.Array(replacements, tree_.expressions.size());
    std::vector<std::optional<SymbolId>> stored;
    budget_.Array(stored, tree_.expressions.size());
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
      if (const auto replacement = plan_.replacements[id.value]) result = stored[replacement->value];
      else if (const auto* read = std::get_if<SymbolExpr>(&node)) {
        if (!plan_.literals[read->symbol.value]) {
          const auto source = plan_.Storage(read->symbol);
          if (MutableBinding(source)) result = source;
        }
      } else if (const auto* assignment = std::get_if<AssignmentExpr>(&node)) {
        if (const auto* target = std::get_if<SymbolTarget>(&assignment->target);
            target && MutableBinding(target->symbol)) {
          result = plan_.Storage(target->symbol);
          plan_.normal_stores[id.value] = InitializedRead(*result, id);
          if (stored[assignment->value.value] == result)
            replacements[id.value] = assignment->value;
        }
      } else if (const auto* update = std::get_if<UpdateExpr>(&node)) {
        if (update->result == UpdateResult::kPrefix && MutableBinding(update->symbol)) result = plan_.Storage(update->symbol);
      } else if (const auto* sequence = std::get_if<SequenceExpr>(&node)) result = stored[sequence->right.value];
      else if (const auto* choice = std::get_if<ConditionalExpr>(&node)) result = join(choice->consequent, choice->alternate);
      else if (const auto* logical = std::get_if<LogicalExpr>(&node)) result = join(logical->left, logical->right);
    }
    // A proposed outer elision uses the RHS normal-result identity, which is
    // preserved by every inner elision. Thus the batch does not depend on
    // proposal application order; no newly rewritten syntax is re-matched.
    for (std::size_t i = 0; i < replacements.size(); ++i) {
      budget_.Step();
      if (replacements[i]) plan_.replacements[i] = replacements[i];
    }
  }
  void AnalyzeEvaluations() {
    // This pass only produces evaluation facts; it cannot rewrite expressions.
    // Completion order is semantic postorder, independent of arena numbering.
    for (std::size_t at = 0; at < completion_count_; ++at) {
      budget_.Step(); const auto id = completions_[at];
      const auto& node = tree_.expressions[id.value].expression;
      auto evaluation = IntrinsicEvaluation(node);
      if (const auto replacement = plan_.replacements[id.value]) evaluation = plan_.evaluations[replacement->value];
      else if (const auto* read = std::get_if<SymbolExpr>(&node)) {
        if (InitializedRead(plan_.Storage(read->symbol), id)) evaluation = ExpressionEvaluation::kInert;
      } else if (const auto* receiver = std::get_if<ThisExpr>(&node)) {
        if (receiver->owner == tree_.owner && tree_.callable != CallableKind::kDerivedConstructor &&
            !IsArrowCallable(tree_.callable)) evaluation = ExpressionEvaluation::kInert;
      }
      VisitExpressionChildren(tree_, node, [&](ExprId child) {
        budget_.Step(); if (plan_.evaluations[child.value] == ExpressionEvaluation::kRequired)
          evaluation = ExpressionEvaluation::kRequired;
      });
      plan_.evaluations[id.value] = evaluation;
    }
  }
  std::size_t OpenRegion() {
    RequireTree(next_region_<regions_.size(),TreeStructureError::kOwnership);
    const auto region=next_region_++;regions_[region].begin=region;return region;
  }
  bool CompletedStore(const BindingUses& use) const {
    if(!use.store||use.first!=use.store||use.escaped||!use.first_region)return false;
    const auto range=regions_[use.store_region];
    return *use.first_region>=range.begin&&*use.last_region<range.end;
  }
  bool PrivateCopy(const BindingUses& use) const {
    return use.private_storage&&use.declaration&&use.writes==1&&CompletedStore(use);
  }
  // Transitional for-let lifetime proof; SourceVariables will consume the
  // shared binding activation/initialization facts, not another scope census.
  bool RootLifetime(SymbolId root,ExprId occurrence) const {
    const auto& use=uses_[root.value];
    if(!use.initializer||!plan_.statements[use.initializer->value])return true;
    // A prospective native declaration may narrow its original lifetime (for
    // let includes test/body/update). Availability must cover substituted reads
    // in the printed owner, not only the canonical symbol's original scope.
    const auto owner=expression_owners_[occurrence.value];
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
  void Constants() {
    // Sparse constant lattice on the common first-store/scope proof. A single
    // static store may execute repeatedly only within its confined lifetime;
    // primitive literals have no identity/allocation/getter/call to replay.
    // Unknown/mutable/captured values remain storage. One source per copy lets
    // an indexed dependency worklist propagate every edge once, independent
    // of symbol/arena/register numbering; never rescan a chain per variable.
    std::size_t ready = 0;
    for (std::size_t i = 0; i < uses_.size(); ++i) {
      budget_.Step();const auto& use = uses_[i];
      if (!PrivateCopy(use)) continue;
      const auto symbol = SymbolId{static_cast<std::uint32_t>(i)};
      const auto assignment = *use.store;
      const auto value = std::get<AssignmentExpr>(tree_.expressions[assignment.value].expression).value;
      const auto& expression = tree_.expressions[value.value].expression;
      if (std::holds_alternative<TreeLiteral>(expression)) {
        plan_.literals[i] = value;constant_queue_[ready++] = symbol;
      } else if (const auto* read = std::get_if<SymbolExpr>(&expression)) {
        constant_next_[i] = constant_heads_[read->symbol.value];constant_heads_[read->symbol.value] = symbol;
      }
    }
    for (std::size_t at = 0; at < ready; ++at) {
      budget_.Step();const auto source = constant_queue_[at];
      for (auto target = constant_heads_[source.value]; target; target = constant_next_[target->value]) {
        budget_.Step();RequireTree(!plan_.literals[target->value] && ready < constant_queue_.size(),TreeStructureError::kOwnership);
        plan_.literals[target->value] = plan_.literals[source.value];constant_queue_[ready++] = *target;
      }
    }
    for (std::size_t i = 0; i < uses_.size(); ++i) {
      budget_.Step();if (!plan_.literals[i]) continue;
      Elide(uses_[i]);
    }
  }
  void Aliases() {
    // Retain one immutable producer; only storage copies disappear. The common
    // lifetime proof composes along copy edges, with no allocation/effect replay.
    // Name resolution at each real read is a separate semantic obligation.
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
      if(use.definition&&!use.writes&&!use.escaped&&use.initialized_first&&
          Confined(use,lexical_[use.definition->value])) {
        roots[i]=symbol;constant_queue_[ready++]=symbol;continue;
      }
      if(use.writes!=1||!CompletedStore(use)||plan_.literals[i])continue;
      if(!PrivateCopy(use)) {
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
    if(!candidates)return;
    std::vector<std::optional<SymbolId>> copy_targets,blocked_sources;
    std::vector<std::uint8_t> blocked;
    std::vector<TreeScopedReference> references;
    std::vector<ExprId> occurrences;
    budget_.Array(copy_targets,tree_.expressions.size());budget_.Array(blocked_sources,uses_.size());
    budget_.Array(blocked,uses_.size());budget_.Array(references,tree_.expressions.size());
    budget_.Array(occurrences,tree_.expressions.size());
    for(std::size_t i=0;i<roots.size();++i) {
      budget_.Step();if(!roots[i]||roots[i]->value==i)continue;
      // A statement store disappears entirely. An embedded store still yields
      // its RHS at that occurrence; lexical resolution remains required there.
      if(!uses_[i].initializer)continue;
      const auto assignment=*uses_[i].store;
      copy_targets[std::get<AssignmentExpr>(tree_.expressions[assignment.value].expression).value.value]=SymbolId{static_cast<std::uint32_t>(i)};
    }
    std::size_t count=0;
    for(std::size_t i=0;i<tree_.expressions.size();++i) {
      budget_.Step();const auto* read=std::get_if<SymbolExpr>(&tree_.expressions[i].expression);
      if(!read||!roots[read->symbol.value]||roots[read->symbol.value]==read->symbol)continue;
      references[count]={*roots[read->symbol.value],expression_scopes_[i]};
      occurrences[count++]={static_cast<std::uint32_t>(i)};
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
      budget_.Step();if(visible[i]&&RootLifetime(references[i].symbol,occurrences[i]))continue;
      const auto occurrence=occurrences[i];const auto source=std::get<SymbolExpr>(tree_.expressions[occurrence.value].expression).symbol;
      // A copy RHS ceases to be a read when its store is elided. Only if the
      // destination must remain does its unavailable root force source storage.
      if(const auto target=copy_targets[occurrence.value])blocked_sources[target->value]=source;
      else retain(source);
    }
    for(std::size_t at=0;at<ready;++at) {
      budget_.Step();if(const auto source=blocked_sources[constant_queue_[at].value])retain(*source);
    }
    for(std::size_t i=0;i<roots.size();++i) {
      budget_.Step();if(!roots[i]||roots[i]->value==i||blocked[i])continue;
      plan_.aliases[i]=roots[i];Elide(uses_[i]);
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
      bool write, std::optional<StmtId> projector) {
    auto& use = uses_[symbol.value]; if (!use.first) use.first = expression;
    use.first_region=use.first_region?std::min(*use.first_region,current_region_):current_region_;
    use.last_region=use.last_region?std::max(*use.last_region,current_region_):current_region_;
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
        completion_order_[visit.expression.value] = completion_count_;
        completions_[completion_count_++] = visit.expression;
        if (const auto* assignment = std::get_if<AssignmentExpr>(&node)) {
          if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) {
            Observe(target->symbol, visit.expression, container, true, projector);
            auto& use=uses_[target->symbol.value];
            const auto& rhs = tree_.expressions[assignment->value.value].expression;
            use.named_creation |= std::holds_alternative<FunctionExpr>(rhs) || std::holds_alternative<ClassExpr>(rhs);
            if(use.definition&&!use.store){use.store=visit.expression;use.store_region=current_region_;}
          }
        }
        if(visit.guarded)regions_[current_region_].end=next_region_;
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
        declaration_controls_[syntax.symbol.value] = control_;
        use.definition=visit.statement;use.initialized_first=!use.first;use.definition_region=current_region_;
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
  std::vector<std::size_t> declaration_controls_, completion_order_;
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
  std::vector<ContainerRange> regions_;
  std::size_t current_region_=0,next_region_=1;
  std::vector<std::optional<SymbolId>> constant_heads_, constant_next_;
  std::vector<SymbolId> constant_queue_;
  std::vector<StatementVisit> statements_;
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
