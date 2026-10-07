#include "binding_initializer.h"
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
  std::optional<StmtId> definition;
  std::size_t definition_region = 0;
  std::optional<ExprId> first;
  std::optional<ExprId> store;
  std::size_t store_region = 0;
  std::optional<std::size_t> first_region, last_region;
  std::optional<std::size_t> first_container, last_container;
  std::size_t writes = 0;
  bool escaped = false, outside_body = false, initialized_first = false;
};
struct ContainerRange { std::size_t begin = 0, end = 0; };
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
    budget.Array(plan_.literals, tree.symbols.size());
    budget.Array(plan_.aliases, tree.symbols.size());
    budget.Array(plan_.replacements, tree.expressions.size());
    budget.Array(plan_.evaluations, tree.expressions.size());
    budget.Array(completions_, tree.expressions.size());
    budget.Array(plan_.entry_counts, loops.initializer_roots.size() + 1);
    budget.Array(constant_heads_, tree.symbols.size());budget.Array(constant_next_, tree.symbols.size());
    budget.Array(constant_queue_, tree.symbols.size());
    budget.Array(uses_, tree.symbols.size()); budget.Array(projected_, tree.statements.size());
    budget.Array(containers_, tree.statements.size());
    budget.Array(lexical_, tree.statements.size()); budget.Array(projectors_, tree.statements.size());
    budget.Array(retained_bodies_, tree.statements.size());
    budget.Array(projection_observations_, tree.expressions.size());
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
    for (const auto& alias : tree_.modules) { budget_.Step(); uses_[alias.symbol.value].escaped = true; }
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
      if (!use.declaration || !FirstStored(use)) continue;
      const auto& effect = std::get<ExpressionStmt>(tree_.statements[use.initializer->value].statement);
      // DFS intervals prove that every observation stays in the new lexical
      // lifetime. A first completed direct store dominates later reads there,
      // including on each loop entry; RHS/earlier reads and sibling catch or
      // finally observations preserve the original undefined/old-value slot.
      // This is scope confinement, never a same-block admission. No SSA merge,
      // expression motion, source const inference or new capture environment.
      plan_.statements[use.initializer->value] = BindingInitializer{SymbolId{static_cast<std::uint32_t>(i)},
          effect.expression, use.writes == 1 ? DeclarationKind::kConst : DeclarationKind::kLet};
      if (!loops_.initializer_owners[use.initializer->value]) plan_.omitted[use.declaration->value] = 1;
    }
    Constants();
    Aliases();
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
    Evaluations();
    return Finish();
  }
 private:
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
  void Evaluations() {
    // Scan's finish order is postorder even when expression IDs are reversed.
    // Each owned child is consumed once. No repeated subtree/type/CFG walk.
    for (std::size_t at = 0; at < completion_count_; ++at) {
      budget_.Step(); const auto id = completions_[at];
      const auto& node = tree_.expressions[id.value].expression;
      auto evaluation = IntrinsicEvaluation(node);
      if (const auto replacement = plan_.replacements[id.value]) evaluation = plan_.evaluations[replacement->value];
      else if (const auto* read = std::get_if<SymbolExpr>(&node)) {
        if (InitializedRead(read->symbol, id)) evaluation = ExpressionEvaluation::kInert;
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
    return use.declaration&&use.writes==1&&CompletedStore(use);
  }
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
  bool FirstStored(const BindingUses& use) const {
    if (!use.initializer || use.escaped) return false;
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
  void Captures(const PreparedFamily& prepared) {
    const auto& family = prepared.Family(); const auto& root = family.members[family.root.value].tree;
    for (const auto& alias : root.captures) {
      budget_.Step(); if (alias.target.owner == tree_.owner) uses_[alias.target.symbol.value].escaped = true;
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
        if (const auto* update = std::get_if<UpdateExpr>(&node)) {
          Observe(update->symbol, visit.expression, container, true, projector);
        }
        if (const auto* created = std::get_if<ClassExpr>(&node)) {
          const auto& definition = *created->definition;
          if (definition.constructor) Captures(*definition.constructor);
          for (const auto& member : definition.members) { budget_.Step(); if (member) Captures(*member); }
          for (const auto& site : definition.definitions) for (const auto& body : site.bodies) {
            budget_.Step(); if (body) Captures(*body);
          }
          if (definition.initializer) for (const auto& alias : definition.initializer->tree.captures) {
            budget_.Step(); if (alias.target.owner == tree_.owner) uses_[alias.target.symbol.value].escaped = true;
          }
        }
      } else {
        RequireTree(completion_count_ < completions_.size(), TreeStructureError::kOwnership);
        completions_[completion_count_++] = visit.expression;
        if (const auto* assignment = std::get_if<AssignmentExpr>(&node)) {
          if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) {
            Observe(target->symbol, visit.expression, container, true, projector);
            auto& use=uses_[target->symbol.value];
            if(use.definition&&!use.store){use.store=visit.expression;use.store_region=current_region_;}
          }
        }
        if(visit.guarded)regions_[current_region_].end=next_region_;
        current_region_=visit.parent_region;
      }
    }
  }
  void Enter(const StatementVisit& visit) {
    const auto& node = tree_.statements[visit.statement.value].statement;
    std::visit([&](const auto& syntax) {
      using T = std::decay_t<decltype(syntax)>;
      if constexpr (std::is_same_v<T, DeclarationStmt>) {
        if (syntax.initializer) Scan(*syntax.initializer, visit.lexical, visit.projector,visit.scope);
        const auto& symbol = tree_.symbols[syntax.symbol.value]; auto& use = uses_[syntax.symbol.value];
        use.definition=visit.statement;use.initialized_first=!use.first;use.definition_region=current_region_;
        if (!syntax.initializer && symbol.kind == BindingKind::kCompilerTemporary &&
            symbol.declaration == DeclarationKind::kLet && visit.container && !visit.projected) {
          use.declaration = visit.statement;
        }
      } else if constexpr (std::is_same_v<T, ExpressionStmt>) {
        Scan(syntax.expression, visit.lexical, visit.projector,visit.scope);
        if (const auto* assignment = std::get_if<AssignmentExpr>(&tree_.expressions[syntax.expression.value].expression))
          if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) {
            auto& use = uses_[target->symbol.value];
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
  }
  const FunctionTree& tree_;
  const LoopSyntax& loops_;
  const NullishSyntax& nullish_;
  TreeCheckBudget& budget_;
  BindingInitializers plan_;
  std::vector<BindingUses> uses_;
  std::vector<std::uint8_t> projected_;
  std::vector<ContainerRange> containers_;
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
