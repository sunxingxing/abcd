#include "switch_syntax.h"

#include "expression_children.h"
namespace abcd::source {
const BinaryExpr& SwitchComparison(const FunctionTree& tree,
                                   const SwitchCaseStmt& clause) {
  RequireTree(
      clause.condition && clause.condition->value < tree.expressions.size(),
      TreeStructureError::kUnsupportedSyntax);
  const auto* expression =
      &tree.expressions[clause.condition->value].expression;
  bool inverted = false;
  if (const auto* unary = std::get_if<UnaryExpr>(expression)) {
    RequireTree(unary->operation == TreeUnary::kNot &&
                    unary->operand.value < tree.expressions.size(),
                TreeStructureError::kUnsupportedSyntax);
    expression = &tree.expressions[unary->operand.value].expression;
    inverted = true;
  }
  const auto* cmp = std::get_if<BinaryExpr>(expression);
  RequireTree(cmp && cmp->operation ==
                         (inverted ? analysis::BinaryOperator::kStrictNotEqual
                                   : analysis::BinaryOperator::kStrictEqual),
              TreeStructureError::kUnsupportedSyntax);
  return *cmp;
}
SwitchSyntax VerifySwitchSyntax(const FunctionTree& tree,
                                const SwitchStmt& node,
                                TreeCheckBudget& budget) {
  RequireTree(node.body.value < tree.statements.size(),
              TreeStructureError::kRange);
  const auto* body =
      std::get_if<BlockStmt>(&tree.statements[node.body.value].statement);
  RequireTree(body && body->first_child <= tree.children.size() &&
                  body->child_count <= tree.children.size() - body->first_child,
              TreeStructureError::kRange);
  std::optional<SwitchSyntax> syntax;
  bool seen_default = false;
  for (std::uint32_t i = 0; i < body->child_count; ++i) {
    budget.Step();
    const auto s = tree.children[body->first_child + i];
    RequireTree(s.value < tree.statements.size(), TreeStructureError::kRange);
    const auto* c =
        std::get_if<SwitchCaseStmt>(&tree.statements[s.value].statement);
    RequireTree(c, TreeStructureError::kUnsupportedSyntax);
    if (!c->condition) {
      RequireTree(!seen_default, TreeStructureError::kUnsupportedSyntax);
      seen_default = true;
      continue;
    }
    const auto& cmp = SwitchComparison(tree, *c);
    RequireTree(cmp.left.value < tree.expressions.size(),
                TreeStructureError::kRange);
    const auto& expression = tree.expressions[cmp.left.value].expression;
    std::optional<SymbolId> cache;
    if (const auto* ref = std::get_if<SymbolExpr>(&expression))
      cache = ref->symbol;
    else if (!syntax)
      if (const auto* assignment = std::get_if<AssignmentExpr>(&expression))
        if (const auto* target = std::get_if<SymbolTarget>(&assignment->target))
          cache = target->symbol;
    RequireTree(cache && cache->value < tree.symbols.size(),
                TreeStructureError::kBinding);
    const auto& binding = tree.symbols[cache->value];
    RequireTree(binding.kind == BindingKind::kCompilerTemporary ||
                    binding.kind == BindingKind::kParameter,
                TreeStructureError::kBinding);
    if (!syntax)
      syntax = SwitchSyntax{cmp.left, *cache};
    else
      RequireTree(syntax->cache == *cache, TreeStructureError::kBinding);
  }
  RequireTree(syntax.has_value() && tree.strictness == Strictness::kStrict,
              TreeStructureError::kUnsupportedSyntax);
  // This check is shared once by structure and recovery. A cache is never a
  // mutable outer binding or observable property; capture could expose writes.
  return *syntax;
}
void VerifyAllSwitchSyntax(const FunctionTree& tree, TreeCheckBudget& budget) {
  bool any = false, any_case = false;
  for (const auto& s : tree.statements) {
    budget.Step();
    any |= std::holds_alternative<SwitchStmt>(s.statement);
    any_case |= std::holds_alternative<SwitchCaseStmt>(s.statement);
  }
  if (!any) {
    RequireTree(!any_case, TreeStructureError::kUnsupportedSyntax);
    return;
  }
  std::vector<std::uint8_t> owned_clauses;
  budget.Array(owned_clauses, tree.statements.size());
  std::vector<std::uint8_t> captured;
  budget.Array(captured, tree.symbols.size());
  for (const auto symbol : tree.closure_captures) {
    budget.Step();
    RequireTree(symbol.value < captured.size(), TreeStructureError::kRange);
    captured[symbol.value] = 1;
  }
  struct Check {
    ExprId expression;
    SymbolId cache;
  };
  std::vector<Check> stack;
  budget.Array(stack, tree.expressions.size());
  for (const auto& s : tree.statements)
    if (const auto* sw = std::get_if<SwitchStmt>(&s.statement)) {
      const auto syntax = VerifySwitchSyntax(tree, *sw, budget);
      RequireTree(!captured[syntax.cache.value], TreeStructureError::kBinding);
      const auto& body =
          std::get<BlockStmt>(tree.statements[sw->body.value].statement);
      std::size_t size = 0;
      for (std::uint32_t c = 0; c < body.child_count; ++c) {
        budget.Step();
        const auto id = tree.children[body.first_child + c];
        RequireTree(!owned_clauses[id.value], TreeStructureError::kOwnership);
        owned_clauses[id.value] = 1;
        const auto& clause =
            std::get<SwitchCaseStmt>(tree.statements[id.value].statement);
        if (clause.condition) {
          RequireTree(size < stack.size(), TreeStructureError::kOwnership);
          stack[size++] = {*clause.condition, syntax.cache};
        }
      }
      while (size) {
        budget.Step();
        const auto item = stack[--size];
        RequireTree(item.expression.value < tree.expressions.size(),
                    TreeStructureError::kRange);
        const auto& expr = tree.expressions[item.expression.value].expression;
        if (const auto* a = std::get_if<AssignmentExpr>(&expr))
          if (const auto* t = std::get_if<SymbolTarget>(&a->target))
            RequireTree(t->symbol != item.cache ||
                            item.expression == syntax.discriminant,
                        TreeStructureError::kBinding);
        if (const auto* u = std::get_if<UpdateExpr>(&expr))
          RequireTree(u->symbol != item.cache, TreeStructureError::kBinding);
        VisitExpressionChildren(tree, expr, [&](ExprId child) {
          RequireTree(size < stack.size(), TreeStructureError::kOwnership);
          stack[size++] = {child, item.cache};
        });
      }
    }
  for (std::size_t i = 0; i < tree.statements.size(); ++i) {
    budget.Step();
    RequireTree(
        bool(owned_clauses[i]) == std::holds_alternative<SwitchCaseStmt>(
                                      tree.statements[i].statement),
        TreeStructureError::kOwnership);
  }
}
}  // namespace abcd::source
