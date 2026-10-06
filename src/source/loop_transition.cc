#include "loop_syntax.h"
#include "statement_children.h"
#include "statement_expressions.h"
#include "expression_children.h"
namespace abcd::source {
// ECMAScript 2024 ForBodyEvaluation/LoopContinues: test, body, increment;
// return/throw/break skip increment. Keep the canonical verified AST unchanged.
// Owned completion coverage and ordered def-use closure choose a program, not
// a ++/register/opcode pattern. Scope/handler boundaries cannot be guessed from
// byte offsets. See loop_transition_test and phase_copy_source_test.
namespace {
struct Definition { std::optional<StmtId> statement; bool multiple = false; };
struct Position { std::uint32_t begin = 0; std::uint32_t end = 0; };
struct Cursor { StmtId statement; std::size_t child = 0; };
}
void PlanLoopTransitions(const FunctionTree& tree, LoopSyntax& plan, TreeCheckBudget& budget,
    std::span<const std::uint8_t> bound) {
  if (tree.statements.empty()) return;
  std::vector<Definition> definitions;
  std::vector<Position> positions;
  std::vector<std::uint32_t> continuations, observed;
  std::vector<StmtId> statements;
  std::vector<ExprId> expressions;
  std::vector<SymbolId> dependencies;
  std::vector<Cursor> visits;
  budget.Array(definitions, tree.symbols.size()); budget.Array(observed, tree.symbols.size());
  budget.Array(positions, tree.statements.size()); budget.Array(continuations, tree.statements.size());
  budget.Array(statements, tree.statements.size()); budget.Array(expressions, tree.expressions.size());
  budget.Array(dependencies, tree.expressions.size()); budget.Array(visits, tree.statements.size());
  std::size_t pending = 1; visits[0] = {tree.body}; std::uint32_t stamp = 0;
  const auto blocks = [&](const BlockStmt& block) {
    return std::span<const StmtId>(tree.children).subspan(block.first_child, block.child_count);
  };
  while (pending) {
    budget.Step(); auto& at = visits[pending - 1];
    if (!at.child) positions[at.statement.value].begin = stamp++;
    const auto child = StatementChild(tree.statements[at.statement.value].statement, at.child++, blocks);
    if (child) { visits[pending++] = {*child}; continue; }
    positions[at.statement.value].end = stamp; --pending;
  }
  const auto define = [&](SymbolId symbol, StmtId statement) {
    budget.Step(); auto& item = definitions[symbol.value];
    if (item.statement && item.statement != statement) item.multiple = true;
    item.statement = statement;
  };
  const auto expression_walk = [&](StmtId statement, auto visit) {
    std::size_t count = 0;
    VisitStatementExpressions(tree.statements[statement.value].statement, [&](ExprId id) { expressions[count++] = id; });
    while (count) {
      budget.Step(); const auto id = expressions[--count]; const auto& node = tree.expressions[id.value].expression;
      visit(node);
      VisitExpressionChildren(tree, node, [&](ExprId child) { expressions[count++] = child; });
    }
  };
  const auto statement_walk = [&](StmtId root, std::optional<StmtId> excluded, auto visit) {
    std::size_t count = 0; statements[count++] = root;
    while (count) {
      budget.Step(); const auto id = statements[--count]; if (id == excluded) continue;
      visit(id);
      StatementChildren(tree, id, [&](StmtId child) { statements[count++] = child; });
    }
  };
  for (std::size_t s = 0; s < tree.statements.size(); ++s) {
    budget.Step(); const StmtId statement{static_cast<std::uint32_t>(s)};
    const auto& syntax = tree.statements[s].statement;
    if (const auto* transfer = std::get_if<ContinueStmt>(&syntax)) ++continuations[transfer->target.value];
    if (const auto* declaration = std::get_if<DeclarationStmt>(&syntax); declaration && declaration->initializer)
      define(declaration->symbol, statement);
    expression_walk(statement, [&](const TreeExpression& expression) {
      if (const auto* assignment = std::get_if<AssignmentExpr>(&expression)) {
        if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) define(target->symbol, statement);
      } else if (const auto* update = std::get_if<UpdateExpr>(&expression)) define(update->symbol, statement);
    });
  }
  for (std::size_t l = 0; l < tree.statements.size(); ++l) {
    budget.Step(); if (!plan.conditions[l]) continue;
    const StmtId loop{static_cast<std::uint32_t>(l)};
    const auto& condition = *plan.conditions[l];
    const auto& body = std::get<LoopStmt>(tree.statements[l].statement);
    const auto& header = std::get<BlockStmt>(tree.statements[body.body.value].statement);
    const auto observation = static_cast<std::uint32_t>(l + 1);
    for (std::uint32_t i = 0; i < condition.header_count; ++i)
      statement_walk(tree.children[header.first_child + i], condition.body, [&](StmtId id) {
        expression_walk(id, [&](const TreeExpression& expression) {
          if (const auto* read = std::get_if<SymbolExpr>(&expression)) observed[read->symbol.value] = observation;
          else if (const auto* read = std::get_if<UpdateExpr>(&expression)) observed[read->symbol.value] = observation;
        });
      });
    StmtId container = body.body;
    std::uint32_t lower = condition.header_count;
    if (lower == header.child_count) {
      if (!condition.body || !std::holds_alternative<BlockStmt>(tree.statements[condition.body->value].statement)) continue;
      container = *condition.body; lower = 0;
    }
    // Transparent language blocks do not alter scope or handler ownership.
    // A declaration, label, nested loop or try is never crossed by this spine.
    while (true) {
      budget.Step(); const auto& block = std::get<BlockStmt>(tree.statements[container.value].statement);
      if (block.label || bound[block.scope.value] || block.child_count <= lower) break;
      const auto last = tree.children[block.first_child + block.child_count - 1];
      const auto* nested = std::get_if<BlockStmt>(&tree.statements[last.value].statement);
      if (!nested || nested->label || bound[nested->scope.value]) break;
      container = last; lower = 0;
    }
    const auto& block = std::get<BlockStmt>(tree.statements[container.value].statement);
    if (block.label || bound[block.scope.value]) continue;
    auto first = block.child_count;
    while (first > lower) {
      budget.Step(); const auto& meaning = plan.meanings[tree.children[block.first_child + first - 1].value];
      if (!meaning.expressible || (meaning.exit && (meaning.transfer != LoopCompletionTransfer::kContinue || meaning.exit != loop))) break;
      --first;
    }
    auto selected = block.child_count;
    for (auto i = first; i < block.child_count; ++i) {
      statement_walk(tree.children[block.first_child + i], {}, [&](StmtId id) {
        expression_walk(id, [&](const TreeExpression& expression) {
          std::optional<SymbolId> written;
          if (const auto* assignment = std::get_if<AssignmentExpr>(&expression)) {
            if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) written = target->symbol;
          } else if (const auto* update = std::get_if<UpdateExpr>(&expression)) written = update->symbol;
          if (written && observed[written->value] == observation) selected = std::min(selected, i);
        });
      });
    }
    if (selected == block.child_count) continue;
    // Extend through unique definitions in the same ordered completion program.
    // Include every intervening operation, preserving effects and exceptional
    // partial writes. Definitions outside it stay in their original body.
    std::uint32_t processed = block.child_count;
    while (processed > selected) {
      const auto i = --processed; std::size_t count = 0;
      statement_walk(tree.children[block.first_child + i], {}, [&](StmtId id) {
        expression_walk(id, [&](const TreeExpression& expression) {
          if (const auto* read = std::get_if<SymbolExpr>(&expression)) dependencies[count++] = read->symbol;
          else if (const auto* read = std::get_if<UpdateExpr>(&expression)) dependencies[count++] = read->symbol;
        });
      });
      for (std::size_t d = 0; d < count; ++d) {
        budget.Step(); const auto& definition = definitions[dependencies[d].value];
        if (!definition.statement || definition.multiple) continue;
        const auto position = positions[definition.statement->value].begin;
        std::uint32_t begin = first, end = block.child_count;
        while (begin < end) {
          budget.Step(); const auto middle = begin + (end - begin) / 2;
          if (positions[tree.children[block.first_child + middle].value].begin <= position) begin = middle + 1;
          else end = middle;
        }
        if (begin == first) continue;
        const auto child = tree.children[block.first_child + begin - 1];
        if (position < positions[child.value].end) selected = std::min(selected, begin - 1);
      }
    }
    std::uint32_t owned = 0;
    for (auto i = selected; i < block.child_count; ++i)
      statement_walk(tree.children[block.first_child + i], {}, [&](StmtId id) {
        if (const auto* transfer = std::get_if<ContinueStmt>(&tree.statements[id.value].statement); transfer && transfer->target == loop) ++owned;
      });
    // Every continue to this owner must reach the original transition program.
    // This is coverage of all transfer identities, never a fixed-count pattern.
    if (owned != continuations[l]) continue;
    auto end = block.child_count;
    while (end > selected && plan.meanings[tree.children[block.first_child + end - 1].value].literal) { budget.Step(); --end; }
    plan.transitions[l] = LoopTransitionSyntax{container, selected, end};
    plan.tail_starts[container.value] = selected;
  }
}
}
