#ifndef ABCD_SOURCE_STATEMENT_CHILDREN_H_
#define ABCD_SOURCE_STATEMENT_CHILDREN_H_

#include <type_traits>

#include "tree_checks.h"

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
    } else if constexpr (std::is_same_v<T, LoopStmt> || std::is_same_v<T, ForInStmt> || std::is_same_v<T, ForOfStmt>) {
      if (index == 0) return node.body;
    } else static_assert(std::is_same_v<T, DeclarationStmt> || std::is_same_v<T, ExpressionStmt> ||
        std::is_same_v<T, ReturnStmt> || std::is_same_v<T, BreakStmt> || std::is_same_v<T, ContinueStmt> ||
        std::is_same_v<T, DebuggerStmt> || std::is_same_v<T, ThrowStmt> || std::is_same_v<T, UnrecoveredStmt>);
    return {};
  }, statement);
}
template<class Visit>
void StatementChildren(const FunctionTree& tree, StmtId id, Visit visit) {
  RequireTree(id.value < tree.statements.size(), TreeStructureError::kRange);
  const auto blocks = [&](const BlockStmt& block) {
    RequireTree(block.first_child <= tree.children.size() &&
        block.child_count <= tree.children.size() - block.first_child, TreeStructureError::kRange);
    return std::span<const StmtId>(tree.children).subspan(block.first_child, block.child_count);
  };
  for (std::size_t index = 0; ; ++index) {
    const auto child = StatementChild(tree.statements[id.value].statement, index, blocks);
    if (!child) break;
    visit(*child);
  }
}
}  // namespace abcd::source
#endif
