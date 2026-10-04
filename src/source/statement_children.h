#ifndef ABCD_SOURCE_STATEMENT_CHILDREN_H_
#define ABCD_SOURCE_STATEMENT_CHILDREN_H_

#include <type_traits>

#include "abcd/source/source_tree.h"

namespace abcd::source {
// Enumerate language-owned statement children; block storage is supplied by
// the actual tree or its completion sequence view. Never infer CFG ownership.
template <class Blocks>
std::optional<StmtId> StatementChild(const TreeStatement& statement, std::size_t index, const Blocks& blocks) {
  return std::visit([&](const auto& node) -> std::optional<StmtId> {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, BlockStmt>) {
      const auto children = blocks(node);
      if (index < children.size()) return children[index];
    } else if constexpr (std::is_same_v<T, IfStmt>) {
      if (index == 0) return node.consequent;
      if (index == 1) return node.alternate;
    } else if constexpr (std::is_same_v<T, TryStmt>) {
      if (index == 0) return node.body;
      if (index == 1 && node.handler) return node.handler->body;
      if (index == (node.handler ? 2U : 1U)) return node.finalizer;
    } else if constexpr (std::is_same_v<T, LoopStmt>) {
      if (index == 0) return node.body;
    } else static_assert(std::is_same_v<T, DeclarationStmt> || std::is_same_v<T, ExpressionStmt> ||
        std::is_same_v<T, ReturnStmt> || std::is_same_v<T, BreakStmt> || std::is_same_v<T, ContinueStmt> ||
        std::is_same_v<T, DebuggerStmt> || std::is_same_v<T, ThrowStmt> || std::is_same_v<T, UnrecoveredStmt>);
    return {};
  }, statement);
}
}  // namespace abcd::source
#endif
