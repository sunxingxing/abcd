#include "tree_labels.h"
#include "tree_checks.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
enum class Visit : std::uint8_t { kUnseen, kActive, kDone };
struct Frame { StmtId statement; std::optional<StmtId> loop; std::size_t next = 0; bool entered = false; };
std::optional<StmtId> Next(const FunctionTree& tree, Frame& frame) {
  const auto& node = tree.statements[frame.statement.value].statement;
  const auto next = frame.next++;
  if (const auto* block = std::get_if<BlockStmt>(&node)) {
    RequireTree(block->first_child <= tree.children.size() &&
        block->child_count <= tree.children.size() - block->first_child, TreeStructureError::kRange);
    if (next < block->child_count) return tree.children[block->first_child + next];
  } else if (const auto* branch = std::get_if<IfStmt>(&node)) {
    if (next == 0) return branch->consequent;
    if (next == 1) return branch->alternate;
  } else if (const auto* region = std::get_if<TryStmt>(&node)) {
    if (next == 0) return region->body;
    if (next == 1 && region->handler) return region->handler->body;
    if (next == (region->handler ? 2U : 1U)) return region->finalizer;
  } else if (const auto* loop = std::get_if<LoopStmt>(&node)) {
    if (next == 0) return loop->body;
  }
  return {};
}
}  // namespace
std::variant<TreeStorage, TreeStructureError> AssignTreeLabels(FunctionTree& tree, analysis::SemanticBudget budget) {
  using Error = TreeStructureError;
  try {
    TreeCheckBudget quota(budget);
    RequireTree(tree.statements.size() <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    std::vector<Visit> visited; std::vector<Frame> stack;
    quota.Array(visited, tree.statements.size()); quota.Array(stack, tree.statements.size());
    std::size_t loops = 0;
    tree.labels.clear();
    for (auto& node : tree.statements) {
      quota.Step();
      std::visit([&](auto& statement) {
        using T = std::decay_t<decltype(statement)>;
        if constexpr (std::is_same_v<T, LoopStmt>) { ++loops; statement.label.reset(); }
        else if constexpr (std::is_same_v<T, BreakStmt> || std::is_same_v<T, ContinueStmt>) statement.label.reset();
      }, node.statement);
    }
    quota.Admit(loops, sizeof(StmtId)); tree.labels.reserve(loops);
    RequireTree(tree.body.value < visited.size(), Error::kRange);
    std::size_t depth = 1; stack[0] = {tree.body, {}};
    while (depth) {
      quota.Step(); auto& frame = stack[depth - 1];
      RequireTree(frame.statement.value < visited.size(), Error::kRange);
      auto& node = tree.statements[frame.statement.value].statement;
      if (!frame.entered) {
        RequireTree(visited[frame.statement.value] == Visit::kUnseen, Error::kOwnership);
        visited[frame.statement.value] = Visit::kActive; frame.entered = true;
        if (std::holds_alternative<LoopStmt>(node)) frame.loop = frame.statement;
        const auto transfer = [&](auto& jump) {
          RequireTree(jump.target.value < visited.size() && visited[jump.target.value] == Visit::kActive, Error::kTarget);
          auto* target = std::get_if<LoopStmt>(&tree.statements[jump.target.value].statement);
          RequireTree(target != nullptr, Error::kTarget);
          if (frame.loop == jump.target) return;
          if (!target->label) {
            target->label = LabelId{static_cast<std::uint32_t>(tree.labels.size())};
            tree.labels.push_back(jump.target);
          }
          jump.label = target->label;
        };
        if (auto* jump = std::get_if<BreakStmt>(&node)) transfer(*jump);
        if (auto* jump = std::get_if<ContinueStmt>(&node)) transfer(*jump);
      }
      if (const auto child = Next(tree, frame)) {
        RequireTree(depth < stack.size(), Error::kOwnership);
        stack[depth++] = {*child, frame.loop};
      } else { visited[frame.statement.value] = Visit::kDone; --depth; }
    }
    for (const auto state : visited) { quota.Step(); RequireTree(state == Visit::kDone, Error::kOwnership); }
    return TreeStorage{quota.Bytes(), quota.Work()};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
