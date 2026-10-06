#include "loop_syntax.h"
#include "statement_children.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
LoopCompletionMeaning Normal() { return {true, true, {}, true}; }
LoopCompletionMeaning Follow(LoopCompletionMeaning left, const LoopCompletionMeaning& right) {
  if (!left.expressible || !right.expressible) return {};
  if (!left.normal) {
    // Unreachable operands can remain in short-circuit syntax. Do not describe
    // their occurrence as effect-free merely because it cannot execute.
    if (!right.literal) left.literal.reset();
    return left;
  }
  if (left.exit && right.exit && (left.exit != right.exit || left.transfer != right.transfer)) return {};
  if (!left.exit) { left.exit = right.exit; left.transfer = right.transfer; }
  left.normal = right.normal;
  if (left.literal && right.literal) left.literal = *left.literal && *right.literal;
  else left.literal.reset();
  return left;
}
struct Visit { StmtId statement; bool finish; };
}
std::variant<LoopSyntax, TreeStructureError> PlanLoopSyntax(
    const FunctionTree& tree, analysis::SemanticBudget limit) {
  try {
    TreeCheckBudget budget(limit);
    LoopSyntax result;
    budget.Array(result.meanings, tree.statements.size());
    budget.Array(result.conditions, tree.statements.size());
    budget.Array(result.transitions, tree.statements.size());
    budget.Array(result.tail_starts, tree.statements.size());
    budget.Array(result.initializers, tree.statements.size());
    budget.Array(result.initializer_owners, tree.statements.size());
    std::vector<std::uint8_t> bound, seen;
    budget.Array(bound, tree.scopes.size()); budget.Array(seen, tree.statements.size());
    for (const auto& symbol : tree.symbols) {
      budget.Step(); RequireTree(symbol.scope.value < bound.size(), TreeStructureError::kScope);
      bound[symbol.scope.value] = 1;
    }
    std::vector<Visit> pending;
    // At most one enter and one leave per uniquely owned statement.
    constexpr std::size_t kVisitsPerStatement = 2;
    RequireTree(tree.statements.size() <= budget.Remaining().bytes / sizeof(Visit) / kVisitsPerStatement,
        TreeStructureError::kResourceLimit);
    budget.Array(pending, tree.statements.size() * kVisitsPerStatement);
    std::size_t count = 0;
    if (!tree.statements.empty()) pending[count++] = {tree.body, false};
    while (count) {
      budget.Step(); const auto visit = pending[--count]; const auto id = visit.statement;
      RequireTree(id.value < tree.statements.size(), TreeStructureError::kRange);
      if (!visit.finish) {
        RequireTree(!seen[id.value], TreeStructureError::kOwnership); seen[id.value] = 1;
        RequireTree(count < pending.size(), TreeStructureError::kOwnership);
        pending[count++] = {id, true};
        StatementChildren(tree, id, [&](StmtId child) {
          budget.Step(); RequireTree(count < pending.size(), TreeStructureError::kOwnership);
          pending[count++] = {child, false};
        });
        continue;
      }
      const auto& node = tree.statements[id.value].statement;
      auto& meaning = result.meanings[id.value];
      std::visit([&](const auto& syntax) {
        using T = std::decay_t<decltype(syntax)>;
        if constexpr (std::is_same_v<T, ExpressionStmt>) meaning = {true, true, {}, {}};
        else if constexpr (std::is_same_v<T, BreakStmt> || std::is_same_v<T, ContinueStmt>) {
          const auto* transfer = &syntax;
          RequireTree(transfer->target.value < tree.statements.size(), TreeStructureError::kTarget);
          meaning = {true, false, transfer->target, false,
              std::is_same_v<T, ContinueStmt> ? LoopCompletionTransfer::kContinue : LoopCompletionTransfer::kBreak};
        } else if constexpr (std::is_same_v<T, BlockStmt>) {
          const auto* block = &syntax;
          RequireTree(block->scope.value < bound.size(), TreeStructureError::kScope);
          if (block->label || bound[block->scope.value]) return;
          meaning = Normal();
          for (std::uint32_t i = 0; i < block->child_count; ++i) {
            budget.Step(); meaning = Follow(meaning, result.meanings[tree.children[block->first_child + i].value]);
          }
        } else if constexpr (std::is_same_v<T, IfStmt>) {
          const auto* branch = &syntax;
          const auto& yes = result.meanings[branch->consequent.value];
          const auto no = branch->alternate ? result.meanings[branch->alternate->value] : Normal();
          if (!yes.expressible || !no.expressible || (yes.exit && no.exit &&
              (yes.exit != no.exit || yes.transfer != no.transfer))) return;
          meaning = {true, yes.normal || no.normal, yes.exit ? yes.exit : no.exit, {},
              yes.exit ? yes.transfer : no.transfer};
        } else if constexpr (std::is_same_v<T, LoopStmt>) {
          const auto* loop = &syntax;
          const auto& body = std::get<BlockStmt>(tree.statements[loop->body.value].statement);
          if (body.label || bound[body.scope.value]) return;
          for (std::uint32_t i = 0; i < body.child_count; ++i) {
            budget.Step(); const auto child = tree.children[body.first_child + i];
            const auto& header = result.meanings[child.value];
            if (header.expressible) {
              if (!header.exit) continue;
              if (header.exit == id && header.transfer == LoopCompletionTransfer::kBreak) result.conditions[id.value] = LoopConditionSyntax{i + 1, {}};
              break;
            }
            if (const auto* branch = std::get_if<IfStmt>(&tree.statements[child.value].statement)) {
              const auto& yes = result.meanings[branch->consequent.value];
              const auto no = branch->alternate ? result.meanings[branch->alternate->value] : Normal();
              const auto exits = [&](const LoopCompletionMeaning& arm) {
                return arm.expressible && !arm.normal && arm.exit == id && arm.transfer == LoopCompletionTransfer::kBreak;
              };
              if (exits(yes)) result.conditions[id.value] = LoopConditionSyntax{i + 1, branch->alternate};
              else if (exits(no)) result.conditions[id.value] = LoopConditionSyntax{i + 1, branch->consequent};
            }
            break;
          }
        } else {
          static_assert(std::is_same_v<T, DeclarationStmt> || std::is_same_v<T, ReturnStmt> ||
              std::is_same_v<T, ThrowStmt> ||
              std::is_same_v<T, TryStmt> || std::is_same_v<T, DebuggerStmt> || std::is_same_v<T, UnrecoveredStmt>);
        }
      }, node);
    }
    PlanLoopTransitions(tree, result, budget, bound);
    PlanLoopInitializers(tree, result, budget, bound);
    result.allocated_bytes = budget.Bytes(); result.work = budget.Work(); return result;
  } catch (TreeStructureError error) { return error;
  } catch (const std::bad_alloc&) { return TreeStructureError::kResourceLimit;
  } catch (const std::length_error&) { return TreeStructureError::kResourceLimit; }
}
}
