#ifndef ABCD_SOURCE_TAIL_COMPLETION_H_
#define ABCD_SOURCE_TAIL_COMPLETION_H_
#include "abcd/source/source_tree.h"
namespace abcd::source {
struct TailCompletion {
  StmtId statement;
  std::optional<ExprId> value;
};
// Native callable fallthrough completes with undefined; a module discards its
// entry completion. Only a direct function-body tail with no evaluated operand
// or a typed undefined literal may disappear. Never omit an earlier/nested
// return, symbol read, await/yield or compound expression. This is a syntax
// projection after tree ownership/flow verification, not an IR control rewrite.
// ECMA-262 2024 14.10.1: async-generator return operands implicitly Await even
// undefined. Keep that job boundary; only its operand-free return may fall through.
inline std::optional<TailCompletion> PureTailCompletion(const FunctionTree& tree) {
  if (tree.body.value >= tree.statements.size()) return {};
  const auto* body = std::get_if<BlockStmt>(&tree.statements[tree.body.value].statement);
  if (!body || !body->child_count || body->first_child > tree.children.size() ||
      body->child_count > tree.children.size() - body->first_child) return {};
  const auto last = tree.children[body->first_child + body->child_count - 1];
  if (last.value >= tree.statements.size()) return {};
  const auto* returned = std::get_if<ReturnStmt>(&tree.statements[last.value].statement);
  if (!returned) return {};
  if (returned->value) {
    if (tree.callable == CallableKind::kAsyncGenerator) return {};
    if (returned->value->value >= tree.expressions.size()) return {};
    const auto* literal = std::get_if<TreeLiteral>(&tree.expressions[returned->value->value].expression);
    if (!literal || !std::holds_alternative<UndefinedLiteral>(*literal)) return {};
  }
  return TailCompletion{last, returned->value};
}
}
#endif
