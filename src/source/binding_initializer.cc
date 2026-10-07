#include "binding_initializer.h"
#include "class_tree.h"
#include "expression_children.h"
#include "loop_syntax.h"
#include "nullish_syntax.h"
#include "statement_children.h"
#include <algorithm>
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
struct BindingUses {
  std::optional<StmtId> declaration, initializer, initializer_container;
  std::optional<ExprId> first;
  std::optional<std::size_t> first_container, last_container;
  std::size_t writes = 0;
  bool escaped = false, outside_body = false;
};
struct ContainerRange { std::size_t begin = 0, end = 0; };
struct ProjectionObservation { SymbolId symbol; StmtId owner; };
struct ExpressionVisit { ExprId expression; bool finish = false; };
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
    budget.Array(plan_.literals, tree.symbols.size());
    budget.Array(plan_.aliases, tree.symbols.size());
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
        --pending; continue;
      }
      const auto container = std::holds_alternative<BlockStmt>(node) ? std::optional{visit.statement} : std::nullopt;
      const auto projected = visit.projected; const auto lexical = visit.lexical; const auto projector = visit.projector;
      const auto scope=visit.scope;
      statements_[pending++] = {*child, container, lexical, projector, 0, projected};
      statements_[pending-1].scope=scope;
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
      if (!use.declaration || !use.initializer || use.escaped || use.outside_body) continue;
      const auto& effect = std::get<ExpressionStmt>(tree_.statements[use.initializer->value].statement);
      // DFS intervals prove that every observation stays in the new lexical
      // lifetime. A first completed direct store dominates later reads there,
      // including on each loop entry; RHS/earlier reads and sibling catch or
      // finally observations preserve the original undefined/old-value slot.
      // This is scope confinement, never a same-block admission. No SSA merge,
      // expression motion, source const inference or new capture environment.
      if (use.first != effect.expression) continue;
      const auto range = containers_[use.initializer_container->value];
      if (*use.first_container < range.begin || *use.last_container >= range.end) continue;
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
    return Finish();
  }
 private:
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
      if (!use.initializer || use.writes != 1 || !plan_.statements[use.initializer->value]) continue;
      const auto symbol = SymbolId{static_cast<std::uint32_t>(i)};
      const auto assignment = plan_.statements[use.initializer->value]->assignment;
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
      plan_.omitted[uses_[i].declaration->value] = 1;plan_.omitted[uses_[i].initializer->value] = 1;
      plan_.statements[uses_[i].initializer->value].reset();
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
      if(!use.initializer||use.writes!=1||!plan_.statements[use.initializer->value])continue;
      const auto assignment=plan_.statements[use.initializer->value]->assignment;
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
      const auto assignment=plan_.statements[uses_[i].initializer->value]->assignment;
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
      budget_.Step();if(visible[i])continue;
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
      plan_.aliases[i]=roots[i];plan_.omitted[uses_[i].declaration->value]=1;
      plan_.omitted[uses_[i].initializer->value]=1;plan_.statements[uses_[i].initializer->value].reset();
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
        expression_scopes_[visit.expression.value]=scope;
        expressions_[pending++] = {visit.expression, true};
        VisitExpressionChildren(tree_, node, [&](ExprId child) {
          budget_.Step(); RequireTree(pending < expressions_.size(), TreeStructureError::kOwnership);
          expressions_[pending++] = {child};
        });
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
      } else if (const auto* assignment = std::get_if<AssignmentExpr>(&node)) {
        if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) Observe(target->symbol, visit.expression, container, true, projector);
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
            if (use.declaration && ((!visit.projected && visit.container) || entry) && !use.initializer) {
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
  std::vector<ScopeId> expression_scopes_;
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
