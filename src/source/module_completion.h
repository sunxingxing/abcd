#ifndef ABCD_SOURCE_MODULE_COMPLETION_H_
#define ABCD_SOURCE_MODULE_COMPLETION_H_
#include "abcd/source/source_tree.h"
namespace abcd::source {
struct ModuleCompletion {
  StmtId statement;
  std::optional<ExprId> value;
};
// Only the terminal, effect-free undefined completion may disappear from a
// module body. Calls, symbol reads and compound expressions remain evaluated.
// The caller establishes the actual module destination; this does not do so.
inline std::optional<ModuleCompletion> PureModuleCompletion(const FunctionTree& tree) {
  if (tree.body.value >= tree.statements.size()) return {};
  const auto* body = std::get_if<BlockStmt>(&tree.statements[tree.body.value].statement);
  if (!body || !body->child_count || body->first_child > tree.children.size() ||
      body->child_count > tree.children.size() - body->first_child) return {};
  const auto last = tree.children[body->first_child + body->child_count - 1];
  if (last.value >= tree.statements.size()) return {};
  const auto* returned = std::get_if<ReturnStmt>(&tree.statements[last.value].statement);
  if (!returned) return {};
  if (returned->value) {
    if (returned->value->value >= tree.expressions.size()) return {};
    const auto* literal = std::get_if<TreeLiteral>(&tree.expressions[returned->value->value].expression);
    if (!literal || !std::holds_alternative<UndefinedLiteral>(*literal)) return {};
  }
  return ModuleCompletion{last, returned->value};
}
}
#endif
