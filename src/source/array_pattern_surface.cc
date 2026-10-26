#include "array_pattern_surface.h"

#include "tree_checks.h"

namespace abcd::source {
namespace {
using analysis::PatternAction;
struct StatementForm {
  ExprId value;
  std::optional<SymbolId> target;
  std::optional<DeclarationKind> declaration;
};
const ArrayPatternExpr* Pattern(const FunctionTree& tree, ExprId id, PatternAction action) {
  const auto* pattern = std::get_if<ArrayPatternExpr>(&tree.expressions[id.value].expression);
  return pattern && pattern->action == action ? pattern : nullptr;
}
// Projection vectors stay empty when their pass found nothing to record.
template <class T> T At(const std::vector<T>& values, std::uint32_t index) {
  return index < values.size() ? values[index] : T{};
}
bool ReadsRecord(const FunctionTree& tree, ExprId operand, SymbolId record) {
  const auto* read = std::get_if<SymbolExpr>(&tree.expressions[operand.value].expression);
  return read && read->symbol == record;
}
}  // namespace

ArrayPatternSurfaces PlanArrayPatternSurfaces(const FunctionTree& tree,
    const BindingInitializers& bindings, const LoopSyntax& loops,
    const ReadableStatements& readable, TreeCheckBudget& budget) {
  ArrayPatternSurfaces out;
  bool present = false;
  for (const auto& node : tree.expressions) {
    budget.Step();
    if (std::holds_alternative<ArrayPatternExpr>(node.expression)) { present = true; break; }
  }
  if (!present) return out;
  budget.Array(out.statements, tree.statements.size());
  budget.Array(out.members, tree.statements.size());
  std::vector<std::uint8_t> bound;
  budget.Array(bound, tree.symbols.size());
  // A retained ExpressionStmt printed exactly as its tree: no omission,
  // motion, replacement, loop-header or snapshot projection touches it.
  const auto form = [&](StmtId id) -> std::optional<StatementForm> {
    const auto s = id.value;
    if (At(loops.initializer_owners, s) || At(bindings.omitted, s) || At(readable.omit_snapshots, s) ||
        At(readable.guards, s) == ReadableGuard::kRemove || At(bindings.statement_values, s) ||
        At(bindings.forwarded_values, s) || At(bindings.undefined_stores, s)) return {};
    const auto* statement = std::get_if<ExpressionStmt>(&tree.statements[s].statement);
    if (!statement) return {};
    const auto expression = statement->expression;
    if (At(bindings.replacements, expression.value) || At(bindings.transports, expression.value)) return {};
    const auto initializer = At(bindings.statements, s);
    if (initializer && initializer->assignment != expression) return {};
    if (const auto* assignment = std::get_if<AssignmentExpr>(&tree.expressions[expression.value].expression)) {
      const auto* target = std::get_if<SymbolTarget>(&assignment->target);
      if (!target || At(bindings.replacements, assignment->value.value) ||
          At(bindings.transports, assignment->value.value)) return {};
      if (!initializer) return StatementForm{assignment->value, target->symbol, {}};
      if (initializer->symbol != target->symbol ||
          (initializer->declaration != DeclarationKind::kLet &&
           initializer->declaration != DeclarationKind::kConst)) return {};
      return StatementForm{assignment->value, target->symbol, initializer->declaration};
    }
    if (initializer) return {};
    return StatementForm{expression, {}, {}};
  };
  for (const auto& node : tree.statements) {
    budget.Step();
    const auto* block = std::get_if<BlockStmt>(&node.statement);
    if (!block) continue;
    for (std::uint32_t i = 0; i < block->child_count; ++i) {
      budget.Step();
      const auto acquire = tree.children[block->first_child + i];
      const auto head = form(acquire);
      if (!head || !head->target) continue;
      const auto* acquisition = Pattern(tree, head->value, PatternAction::kAcquire);
      if (!acquisition) continue;
      const auto record = *head->target;
      const auto first = static_cast<std::uint32_t>(out.targets.size());
      std::optional<StmtId> close;
      bool declared = false, assigned = false, mutable_binding = false, distinct = true;
      std::uint32_t j = i + 1;
      for (; j < block->child_count; ++j) {
        budget.Step();
        const auto member = tree.children[block->first_child + j];
        const auto current = form(member);
        if (!current) break;
        if (const auto* step = Pattern(tree, current->value, PatternAction::kStep)) {
          if (!ReadsRecord(tree, step->operand, record)) break;
          if (current->target) {
            (current->declaration ? declared : assigned) = true;
            mutable_binding |= current->declaration == DeclarationKind::kLet;
            const auto storage = bindings.Storage(*current->target);
            distinct &= !bound[storage.value];
            bound[storage.value] = 1;
          }
          out.targets.push_back({member, current->target});
          continue;
        }
        if (const auto* end = Pattern(tree, current->value, PatternAction::kClose);
            end && !current->target && ReadsRecord(tree, end->operand, record)) close = member;
        break;
      }
      for (std::uint32_t k = first; k < out.targets.size(); ++k) {
        budget.Step();
        if (out.targets[k].symbol) bound[bindings.Storage(*out.targets[k].symbol).value] = 0;
      }
      // Mixed forms would redeclare an outer binding; a declaration list
      // cannot bind one storage twice. Both stay refused at their nodes.
      if (!close || (declared && (assigned || !distinct))) {
        out.targets.resize(first);
        continue;
      }
      ArrayPatternSurface surface{acquire, *close, acquisition->operand, first,
          static_cast<std::uint32_t>(out.targets.size() - first), {}};
      if (declared) surface.declaration = mutable_binding ? DeclarationKind::kLet : DeclarationKind::kConst;
      out.statements[acquire.value] = static_cast<std::uint32_t>(out.patterns.size());
      out.patterns.push_back(surface);
      for (std::uint32_t k = first; k < out.targets.size(); ++k) {
        budget.Step(); out.members[out.targets[k].statement.value] = 1;
      }
      out.members[close->value] = 1;
      i = j;
    }
  }
  budget.Admit(out.patterns.capacity(), sizeof(ArrayPatternSurface));
  budget.Admit(out.targets.capacity(), sizeof(ArrayPatternTarget));
  return out;
}
}  // namespace abcd::source
