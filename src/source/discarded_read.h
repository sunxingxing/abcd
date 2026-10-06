#ifndef ABCD_SOURCE_DISCARDED_READ_H_
#define ABCD_SOURCE_DISCARDED_READ_H_
#include "abcd/source/source_tree.h"
namespace abcd::source {
// ECMA-262 environment bindings: native parameters/self are initialized before
// their bodies, and reading them does not invoke user code. A discarded direct
// read is a syntax no-op. Mutable locals/cells/modules may throw on TDZ reads;
// never erase those. Keep this shared projection in printer and source transport
// so the original instruction/event keeps its own zero-width source position.
inline std::optional<ExprId> DiscardedInvocationRead(const FunctionTree& tree, StmtId id) {
  if (id.value >= tree.statements.size()) return {};
  const auto* statement = std::get_if<ExpressionStmt>(&tree.statements[id.value].statement);
  if (!statement || statement->expression.value >= tree.expressions.size()) return {};
  const auto* read = std::get_if<SymbolExpr>(&tree.expressions[statement->expression.value].expression);
  if (!read || read->symbol.value >= tree.symbols.size()) return {};
  const auto kind = tree.symbols[read->symbol.value].kind;
  if (kind != BindingKind::kParameter && kind != BindingKind::kFunctionSelf) return {};
  return statement->expression;
}
}
#endif
