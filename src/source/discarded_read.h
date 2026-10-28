#ifndef ABCD_SOURCE_DISCARDED_READ_H_
#define ABCD_SOURCE_DISCARDED_READ_H_
#include "abcd/source/source_tree.h"
namespace abcd::source {
// ECMA-262 environment bindings: native parameters/self are initialized before
// their bodies, and reading them does not invoke user code. Ordinary native
// this is initialized too; derived this can throw before super and stays owned.
// A discarded direct read is a syntax no-op. Mutable locals/cells/modules may throw on TDZ reads;
// never erase those. Keep this shared projection in printer and source transport
// so the original instruction/event keeps its own zero-width source position.
inline std::optional<ExprId> DiscardedInvocationRead(const FunctionTree& tree, StmtId id) {
  if (id.value >= tree.statements.size()) return {};
  const auto* statement = std::get_if<ExpressionStmt>(&tree.statements[id.value].statement);
  if (!statement || statement->expression.value >= tree.expressions.size()) return {};
  if (const auto* receiver = std::get_if<ThisExpr>(&tree.expressions[statement->expression.value].expression);
      receiver && receiver->owner == tree.owner && tree.callable != CallableKind::kDerivedConstructor &&
      !IsArrowCallable(tree.callable)) return statement->expression;
  const auto* read = std::get_if<SymbolExpr>(&tree.expressions[statement->expression.value].expression);
  if (!read || read->symbol.value >= tree.symbols.size()) return {};
  const auto kind = tree.symbols[read->symbol.value].kind;
  if (kind != BindingKind::kParameter && kind != BindingKind::kFunctionSelf) return {};
  return statement->expression;
}
// `return void 0;` prints as `return;`: both complete with undefined. Not in
// async generators, where `return expr` performs Await(expr) (one more job)
// and a bare `return;` does not. The literal keeps its own zero-width source
// position. Program bodies print their completion differently.
inline std::optional<ExprId> ElidedUndefinedReturn(const FunctionTree& tree, StmtId id) {
  if (id.value >= tree.statements.size() || tree.callable == CallableKind::kAsyncGenerator) return {};
  const auto* returned = std::get_if<ReturnStmt>(&tree.statements[id.value].statement);
  if (!returned || !returned->value || returned->value->value >= tree.expressions.size()) return {};
  const auto* literal = std::get_if<TreeLiteral>(&tree.expressions[returned->value->value].expression);
  if (!literal || !std::holds_alternative<UndefinedLiteral>(*literal)) return {};
  return *returned->value;
}
}
#endif
