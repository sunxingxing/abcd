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
};
class Initializers {
 public:
  Initializers(const FunctionTree& tree, const LoopSyntax& loops, const NullishSyntax& nullish,
      TreeCheckBudget& budget) : tree_(tree), loops_(loops), nullish_(nullish), budget_(budget) {
    budget.Array(plan_.statements, tree.statements.size()); budget.Array(plan_.omitted, tree.statements.size());
    budget.Array(uses_, tree.symbols.size()); budget.Array(projected_, tree.statements.size());
    budget.Array(containers_, tree.statements.size());
    budget.Array(lexical_, tree.statements.size()); budget.Array(projectors_, tree.statements.size());
    budget.Array(projection_observations_, tree.expressions.size());
    budget.Array(expressions_, tree.expressions.size()); budget.Array(statements_, tree.statements.size());
  }
  BindingInitializers Run() {
    if (!tree_.recovery_issues.empty()) return Finish();
    for (auto symbol : tree_.closure_captures) { budget_.Step(); uses_[symbol.value].escaped = true; }
    for (const auto& alias : tree_.modules) { budget_.Step(); uses_[alias.symbol.value].escaped = true; }
    for (const auto& parameter : tree_.parameters) {
      budget_.Step(); if (parameter.initializer) Scan(*parameter.initializer, {});
    }
    // Expressions already projected to loop/nullish programs cannot host a
    // statement declaration. This is a grammar gap, not a recovery admission.
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
    std::size_t pending = 1; statements_[0] = {tree_.body};
    while (pending) {
      budget_.Step(); auto& visit = statements_[pending - 1];
      const auto& node = tree_.statements[visit.statement.value].statement;
      if (!visit.entered) {
        visit.entered = true; visit.projected |= projected_[visit.statement.value] != 0;
        if (!visit.projector) visit.projector = projectors_[visit.statement.value];
        if (std::holds_alternative<BlockStmt>(node)) {
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
        if (std::holds_alternative<BlockStmt>(node)) containers_[visit.statement.value].end = next_container_;
        --pending; continue;
      }
      const auto container = std::holds_alternative<BlockStmt>(node) ? std::optional{visit.statement} : std::nullopt;
      const auto projected = visit.projected; const auto lexical = visit.lexical; const auto projector = visit.projector;
      statements_[pending++] = {*child, container, lexical, projector, 0, projected};
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
      plan_.omitted[use.declaration->value] = 1;
    }
    return Finish();
  }
 private:
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
  void Scan(ExprId root, std::optional<StmtId> container, std::optional<StmtId> projector = {}) {
    std::size_t pending = 1; expressions_[0] = {root};
    while (pending) {
      budget_.Step(); const auto visit = expressions_[--pending];
      const auto& node = tree_.expressions[visit.expression.value].expression;
      if (!visit.finish) {
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
        if (syntax.initializer) Scan(*syntax.initializer, visit.lexical, visit.projector);
        const auto& symbol = tree_.symbols[syntax.symbol.value]; auto& use = uses_[syntax.symbol.value];
        if (!syntax.initializer && symbol.kind == BindingKind::kCompilerTemporary &&
            symbol.declaration == DeclarationKind::kLet && visit.container && !visit.projected) {
          use.declaration = visit.statement;
        }
      } else if constexpr (std::is_same_v<T, ExpressionStmt>) {
        Scan(syntax.expression, visit.lexical, visit.projector);
        if (const auto* assignment = std::get_if<AssignmentExpr>(&tree_.expressions[syntax.expression.value].expression))
          if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) {
            auto& use = uses_[target->symbol.value];
            if (use.declaration && visit.container && !visit.projected && !use.initializer) {
              use.initializer = visit.statement; use.initializer_container = visit.container;
            }
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
  std::vector<std::uint8_t> projected_;
  std::vector<ContainerRange> containers_;
  std::vector<std::optional<StmtId>> lexical_, projectors_;
  std::vector<ProjectionObservation> projection_observations_;
  std::size_t projection_count_ = 0;
  std::size_t next_container_ = 0;
  std::vector<ExpressionVisit> expressions_;
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
