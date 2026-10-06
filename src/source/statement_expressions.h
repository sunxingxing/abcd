#ifndef ABCD_SOURCE_STATEMENT_EXPRESSIONS_H_
#define ABCD_SOURCE_STATEMENT_EXPRESSIONS_H_
#include "abcd/source/source_tree.h"
#include <type_traits>
namespace abcd::source {
template<class Visit>
void VisitStatementExpressions(const TreeStatement& statement, Visit visit) {
  std::visit([&](const auto& node) {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, ExpressionStmt>) visit(node.expression);
    else if constexpr (std::is_same_v<T, IfStmt>) visit(node.condition);
    else if constexpr (std::is_same_v<T, DeclarationStmt>) { if (node.initializer) visit(*node.initializer); }
    else if constexpr (std::is_same_v<T, ReturnStmt>) { if (node.value) visit(*node.value); }
    else if constexpr (std::is_same_v<T, ThrowStmt>) visit(node.value);
    else static_assert(std::is_same_v<T, BlockStmt> || std::is_same_v<T, LoopStmt> ||
        std::is_same_v<T, TryStmt> || std::is_same_v<T, BreakStmt> || std::is_same_v<T, ContinueStmt> ||
        std::is_same_v<T, DebuggerStmt> || std::is_same_v<T, UnrecoveredStmt>);
  }, statement);
}
}
#endif
