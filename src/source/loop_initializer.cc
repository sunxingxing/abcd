#include "loop_syntax.h"
#include "statement_children.h"
#include "statement_expressions.h"
#include "expression_children.h"
namespace abcd::source {
// ECMA-262 2024 ForLoopEvaluation: expression initialization runs once before
// the first test, even for zero iterations, without per-iteration bindings.
// Keep the original ordered effects and all intervening writes. Empty lexical
// blocks can separate the roots from the loop; declarations, labels, handlers
// and control-dependent entries delimit the program. Never inspect a compiler
// sequence, storage spelling or CFG shape. See loop_initializer_source_test.
namespace {
struct Definition { std::uint32_t epoch = 0; std::uint32_t root = 0; };
struct Cursor {
  StmtId statement;
  std::optional<BlockBoundary> boundary;
  std::size_t child = 0;
  bool entered = false;
  std::uint32_t first = 0;
  std::uint32_t end = 0;
  std::uint64_t write_begin = 0;
};
}
void PlanLoopInitializers(const FunctionTree& tree, LoopSyntax& plan, TreeCheckBudget& budget,
    std::span<const std::uint8_t> bound) {
  if (tree.statements.empty()) return;
  std::vector<Definition> definitions;
  std::vector<std::uint32_t> observed;
  std::vector<std::uint64_t> last_write;
  std::vector<StmtId> statements;
  std::vector<ExprId> expressions;
  std::vector<Cursor> visits;
  budget.Array(plan.initializer_roots, tree.statements.size());
  budget.Array(definitions, tree.symbols.size()); budget.Array(observed, tree.symbols.size());
  budget.Array(last_write, tree.symbols.size());
  budget.Array(statements, tree.statements.size()); budget.Array(expressions, tree.expressions.size());
  budget.Array(visits, tree.statements.size());
  const auto direct = [&](StmtId id, auto visit) {
    std::size_t pending = 0;
    VisitStatementExpressions(tree.statements[id.value].statement, [&](ExprId expression) { expressions[pending++] = expression; });
    while (pending) {
      budget.Step(); const auto& expression = tree.expressions[expressions[--pending].value].expression;
      visit(expression);
      VisitExpressionChildren(tree, expression, [&](ExprId child) { expressions[pending++] = child; });
    }
  };
  const auto walk = [&](StmtId root, std::optional<StmtId> excluded, auto visit) {
    std::size_t count = 0; statements[count++] = root;
    while (count) {
      budget.Step(); const auto id = statements[--count]; if (id == excluded) continue;
      direct(id, visit);
      StatementChildren(tree, id, [&](StmtId child) { statements[count++] = child; });
    }
  };
  const auto reads = [](const TreeExpression& expression, auto visit) {
    if (const auto* read = std::get_if<SymbolExpr>(&expression)) visit(read->symbol);
    else if (const auto* update = std::get_if<UpdateExpr>(&expression)) visit(update->symbol);
  };
  const auto writes = [](const TreeExpression& expression, auto visit) {
    if (const auto* assignment = std::get_if<AssignmentExpr>(&expression)) {
      if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) visit(target->symbol);
    } else if (const auto* update = std::get_if<UpdateExpr>(&expression)) visit(update->symbol);
  };
  std::uint32_t first = 0, end = 0;
  const auto select = [&](StmtId loop, std::uint32_t first, std::uint32_t end, std::uint64_t write_begin) {
    if (!plan.conditions[loop.value] || first == end) return;
    const auto epoch = loop.value + 1;
    const auto& condition = *plan.conditions[loop.value];
    const auto& body = std::get<LoopStmt>(tree.statements[loop.value].statement);
    const auto& block = std::get<BlockStmt>(tree.statements[body.body.value].statement);
    for (std::uint32_t i = 0; i < condition.header_count; ++i)
      walk(tree.children[block.first_child + i], condition.body, [&](const TreeExpression& expression) {
        reads(expression, [&](SymbolId symbol) { if (last_write[symbol.value] >= write_begin) observed[symbol.value] = epoch; });
      });
    auto selected = end;
    for (auto i = first; i < end; ++i) {
      walk(plan.initializer_roots[i].statement, {}, [&](const TreeExpression& expression) {
        writes(expression, [&](SymbolId symbol) {
          auto& definition = definitions[symbol.value];
          if (definition.epoch != epoch) definition = {epoch, i};
          if (observed[symbol.value] == epoch) selected = std::min(selected, i);
        });
      });
    }
    // Earliest candidate definitions conservatively retain overwritten writes.
    // The suffix grows only leftward; every included root is read once. Including
    // everything between dependency and seed preserves effects/partial writes.
    auto processed = end;
    while (processed > selected) {
      walk(plan.initializer_roots[--processed].statement, {}, [&](const TreeExpression& expression) {
        reads(expression, [&](SymbolId symbol) {
          const auto& definition = definitions[symbol.value];
          if (definition.epoch == epoch) selected = std::min(selected, definition.root);
        });
      });
    }
    if (selected == end) return;
    plan.initializers[loop.value] = LoopInitializerSyntax{selected, end};
    for (auto i = selected; i < end; ++i) {
      budget.Step(); plan.initializer_owners[plan.initializer_roots[i].statement.value] = loop;
    }
  };
  const auto blocks = [&](const BlockStmt& block) {
    return std::span<const StmtId>(tree.children).subspan(block.first_child, block.child_count);
  };
  std::uint64_t write_stamp = 0;
  const auto written = [&](const TreeExpression& expression) {
    writes(expression, [&](SymbolId symbol) { last_write[symbol.value] = ++write_stamp; });
  };
  std::size_t pending = 1; visits[0] = {tree.body, {}};
  while (pending) {
    budget.Step(); auto& cursor = visits[pending - 1];
    const auto& node = tree.statements[cursor.statement.value].statement;
    const auto* block = std::get_if<BlockStmt>(&node);
    if (!cursor.entered) {
      const auto& meaning = plan.meanings[cursor.statement.value];
      if (meaning.expressible && meaning.normal && !meaning.exit) {
        if (cursor.boundary) plan.initializer_roots[end++] = {cursor.statement, *cursor.boundary};
        walk(cursor.statement, {}, written);
        --pending; continue;
      }
      if (std::holds_alternative<LoopStmt>(node)) {
        // The ownership traversal observes every write once. At loop exit a
        // later stamp means a write in its subtree, including nested loops;
        // this selects carried bindings without walking each loop's body again.
        cursor.first = first; cursor.end = end; cursor.write_begin = write_stamp + 1;
      }
      direct(cursor.statement, written);
      if (const auto* declaration = std::get_if<DeclarationStmt>(&node)) last_write[declaration->symbol.value] = ++write_stamp;
      if (!block || block->label || bound[block->scope.value]) first = end;
      cursor.entered = true;
    }
    const auto index = cursor.child++;
    const auto child = StatementChild(node, index, blocks);
    if (child) {
      if (!block) first = end;
      visits[pending++] = {*child, block ? std::optional<BlockBoundary>{{cursor.statement, static_cast<std::uint32_t>(index)}} : std::nullopt};
    } else {
      if (std::holds_alternative<LoopStmt>(node)) select(cursor.statement, cursor.first, cursor.end, cursor.write_begin);
      if (!block || block->label || bound[block->scope.value]) first = end;
      --pending;
    }
  }
  plan.initializer_roots.resize(end);
}
}
