#include "completion_tree.h"

#include <algorithm>
#include <string_view>

namespace abcd::source {
namespace {
void Require(bool value) {
  if (!value) throw TreeFlowError::kControl;
}
using Lists = std::vector<std::vector<StmtId>>;
Lists Children(const FunctionTree& tree, TreeCheckBudget& budget) {
  Lists lists;
  budget.Array(lists, tree.statements.size());
  budget.Admit(tree.children.size(), sizeof(StmtId));
  for (std::size_t i = 0; i < tree.statements.size(); ++i) {
    budget.Step();
    if (const auto* block =
            std::get_if<BlockStmt>(&tree.statements[i].statement))
      lists[i].assign(
          tree.children.begin() + block->first_child,
          tree.children.begin() + block->first_child + block->child_count);
  }
  return lists;
}
struct Position {
  StmtId block;
  std::uint32_t index;
};
struct SequenceEdit {
  StmtId block;
  std::uint32_t first, end;
  std::vector<StmtId> replacement;
};
void EditSequences(Lists& lists, std::vector<SequenceEdit>& edits,
                   TreeCheckBudget& budget) {
  std::sort(edits.begin(), edits.end(), [&](const auto& a, const auto& b) {
    budget.Step();
    return a.block != b.block ? a.block < b.block : a.first < b.first;
  });
  // Rebuild each touched sibling sequence once, rather than shifting its
  // entire remaining suffix for every recovered try. O(C + R log R).
  for (std::size_t first = 0; first < edits.size();) {
    const auto block = edits[first].block;
    const auto& before = lists[block.value];
    auto end = first;
    std::size_t size = before.size();
    while (end < edits.size() && edits[end].block == block) {
      const auto& edit = edits[end++];
      Require(edit.first <= edit.end && edit.end <= before.size());
      size -= edit.end - edit.first;
      size += edit.replacement.size();
    }
    budget.Admit(size, sizeof(StmtId));
    std::vector<StmtId> after;
    after.reserve(size);
    std::size_t cursor = 0;
    for (auto i = first; i < end; ++i) {
      const auto& edit = edits[i];
      Require(cursor <= edit.first);
      after.insert(after.end(), before.begin() + cursor,
                   before.begin() + edit.first);
      after.insert(after.end(), edit.replacement.begin(),
                   edit.replacement.end());
      cursor = edit.end;
    }
    after.insert(after.end(), before.begin() + cursor, before.end());
    budget.Step(before.size() + after.size());
    lists[block.value] = std::move(after);
    first = end;
  }
}
std::vector<std::optional<Position>> Positions(const Lists& lists,
                                               TreeCheckBudget& budget) {
  std::vector<std::optional<Position>> positions;
  budget.Array(positions, lists.size());
  for (std::size_t b = 0; b < lists.size(); ++b) {
    budget.Step();
    for (std::size_t i = 0; i < lists[b].size(); ++i) {
      budget.Step();
      const auto statement = lists[b][i];
      Require(statement.value < positions.size() &&
              !positions[statement.value]);
      positions[statement.value] = Position{
          StmtId{static_cast<std::uint32_t>(b)}, static_cast<std::uint32_t>(i)};
    }
  }
  return positions;
}
// Only a final Block/If path may replace a private preserving RETURN with
// native pending-completion fallthrough. Loops/try/finally are not transparent
// tails: removing their terminal could repeat effects or change cleanup.
std::vector<std::optional<StmtId>> TailParents(const FunctionTree& tree,
    const Lists& lists, TreeCheckBudget& budget) {
  std::vector<std::optional<StmtId>> parents;
  budget.Array(parents, lists.size());
  const auto bind = [&](StmtId child, StmtId parent) {
    Require(child.value < parents.size() && !parents[child.value]); parents[child.value] = parent;
  };
  for (std::uint32_t i = 0; i < lists.size(); ++i) {
    budget.Step();
    if (!lists[i].empty()) bind(lists[i].back(), {i});
    if (const auto* branch = std::get_if<IfStmt>(&tree.statements[i].statement)) {
      bind(branch->consequent, {i});
      if (branch->alternate) bind(*branch->alternate, {i});
    }
  }
  return parents;
}
bool TailWithin(StmtId root, StmtId child,
    const std::vector<std::optional<StmtId>>& parents, TreeCheckBudget& budget) {
  for (std::size_t depth = 0; depth <= parents.size(); ++depth) {
    budget.Step();
    if (root == child) return true;
    Require(child.value < parents.size());
    if (!parents[child.value]) return false;
    child = *parents[child.value];
  }
  Require(false); return false;
}
AnchorPosition Remap(const AnchorPosition& position, const Lists& before,
                     const std::vector<std::optional<Position>>& after) {
  const auto* boundary = std::get_if<BlockBoundary>(&position);
  if (!boundary) return position;
  const auto& children = before[boundary->block.value];
  Require(boundary->index <= children.size());
  if (children.empty()) return position;
  const bool end = boundary->index == children.size();
  const auto statement = children[end ? boundary->index - 1 : boundary->index];
  Require(after[statement.value].has_value());
  const auto target = *after[statement.value];
  return BlockBoundary{target.block, target.index + (end ? 1U : 0U)};
}
std::vector<StmtId> Roots(const FinalizerRegion& region, const TreeFlow& flow,
                          bool exceptional, TreeCheckBudget& budget) {
  std::vector<StmtId> roots;
  budget.Admit(region.cleanup.size() + (region.transfer ? 1 : 0), sizeof(StmtId));
  roots.reserve(region.cleanup.size() + (region.transfer ? 1 : 0));
  for (const auto& pair : region.cleanup) {
    budget.Step();
    const auto op = exceptional ? pair.exceptional : pair.normal;
    if (flow.statements[op.value]) roots.push_back(*flow.statements[op.value]);
  }
  if (region.transfer) {
    const auto id = exceptional ? region.transfer->exceptional : region.transfer->normal;
    Require(id.value >= flow.first_control.value &&
        id.value - flow.first_control.value < flow.control_statements.size());
    roots.push_back(flow.control_statements[id.value - flow.first_control.value]);
  }
  return roots;
}
bool Leaf(const FunctionTree& tree, ExprId id) {
  const auto& expression = tree.expressions[id.value].expression;
  return std::holds_alternative<SymbolExpr>(expression) ||
         std::holds_alternative<TreeLiteral>(expression);
}
// A fresh wrapper has no bindings of its own. Only the immediate lexical
// children change parent; their internal cells and occurrence IDs stay put.
void Reparent(FunctionTree& tree, StmtId id, ScopeId parent) {
  const auto block = [&](StmtId child) {
    const auto& value =
        std::get<BlockStmt>(tree.statements[child.value].statement);
    tree.scopes[value.scope.value].parent = parent;
  };
  const auto& statement = tree.statements[id.value].statement;
  if (std::holds_alternative<BlockStmt>(statement))
    block(id);
  else if (const auto* branch = std::get_if<IfStmt>(&statement)) {
    block(branch->consequent);
    if (branch->alternate) block(*branch->alternate);
  } else if (const auto* loop = std::get_if<LoopStmt>(&statement))
    block(loop->body);
  else if (const auto* region = std::get_if<TryStmt>(&statement)) {
    block(region->body);
    if (region->handler) block(region->handler->body);
    if (region->finalizer) block(*region->finalizer);
  }
}
bool LeafCompletion(const FunctionTree& tree, StmtId id) {
  const auto& statement = tree.statements[id.value].statement;
  if (const auto* returned = std::get_if<ReturnStmt>(&statement))
    return !returned->value || Leaf(tree, *returned->value);
  if (const auto* thrown = std::get_if<ThrowStmt>(&statement)) return Leaf(tree, thrown->value);
  return false;
}
bool Tail(const std::vector<StmtId>& children, std::size_t first,
          const std::vector<StmtId>& cleanup, StmtId exit) {
  return first + cleanup.size() + 1 == children.size() &&
         children.back() == exit &&
         std::equal(cleanup.begin(), cleanup.end(), children.begin() + first);
}
bool SnapshotCopies(std::span<const StmtId> copies,
                    const FinalizerRegion& region, const TreeFlow& flow,
                    const FunctionTree& tree, std::vector<std::size_t>& writes,
                    TreeCheckBudget& budget) {
  const auto stamp = std::size_t(region.region.value) + 1;
  for (const auto& pair : region.cleanup) {
    budget.Step();
    if (const auto target = flow.snapshots[pair.normal.value])
      writes[target->value] = stamp;
  }
  const auto private_symbol = [&](SymbolId id) {
    const auto& symbol = tree.symbols[id.value];
    return symbol.kind == BindingKind::kCompilerTemporary &&
           symbol.scope == tree.body_scope &&
           symbol.declaration == DeclarationKind::kLet;
  };
  for (const auto id : copies) {
    budget.Step();
    const auto* statement =
        std::get_if<ExpressionStmt>(&tree.statements[id.value].statement);
    if (!statement) return false;
    const auto* assignment = std::get_if<AssignmentExpr>(
        &tree.expressions[statement->expression.value].expression);
    if (!assignment) return false;
    const auto* target = std::get_if<SymbolTarget>(&assignment->target);
    if (!target || !private_symbol(target->symbol)) return false;
    const auto reason = tree.symbols[target->symbol.value].temporary;
    // Only single-definition snapshots, not mutable join/staging slots. Fresh
    // operand checks still prove their exact input SSA identity and dominance.
    if (!reason || reason == TemporaryReason::kPhiJoin ||
        reason == TemporaryReason::kParallelCopyCycle ||
        reason == TemporaryReason::kExceptionSnapshot ||
        writes[target->symbol.value] == stamp)
      return false;
    const auto& value = tree.expressions[assignment->value.value].expression;
    if (const auto* source = std::get_if<SymbolExpr>(&value)) {
      if ((!private_symbol(source->symbol) &&
           tree.symbols[source->symbol.value].kind !=
               BindingKind::kParameter) ||
          writes[source->symbol.value] == stamp)
        return false;
    } else if (!std::holds_alternative<TreeLiteral>(value))
      return false;
  }
  return true;
}
}  // namespace

void RecoverFinalizers(const CompletionPlan& plan, const TreeFlow& flow,
                       FunctionTree& tree, TreeCheckBudget& budget) {
  if (plan.finalizers.empty() && plan.yield_returns.empty()) return;
  Require(tree.finalizers.empty() && tree.yield_returns.empty());
  const auto before = Children(tree, budget);
  auto lists = Children(tree, budget);
  const auto positions = Positions(before, budget);
  std::vector<std::size_t> cleanup_writes;
  budget.Array(cleanup_writes, tree.symbols.size());
  // Reserve before taking views: moving an SSO string invalidates its view.
  // Monotone suffixes keep generated names unique; only input names need an
  // index. Sorted lookup has no attacker-controlled hash collision behavior.
  const auto new_names = plan.finalizers.size() + 2 * plan.yield_returns.size();
  budget.Admit(tree.names.size() + new_names,
               sizeof(std::u16string));
  tree.names.reserve(tree.names.size() + new_names);
  std::vector<std::u16string_view> names;
  budget.Array(names, tree.names.size());
  for (std::size_t i = 0; i < names.size(); ++i) names[i] = tree.names[i];
  std::sort(names.begin(), names.end(), [&](auto left, auto right) {
    budget.Step();
    return left < right;
  });
  std::size_t suffix = 0;
  const auto statement = [&](TreeStatement value, TreeOriginId origin) {
    const StmtId id{static_cast<std::uint32_t>(tree.statements.size())};
    budget.Admit(1, sizeof(StmtNode) + sizeof(std::vector<StmtId>));
    budget.Step();
    tree.statements.push_back({std::move(value), origin});
    lists.emplace_back();
    return id;
  };
  const auto block = [&](ScopeId parent, TreeOriginId origin,
                         std::vector<StmtId> children) {
    const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};
    budget.Admit(1, sizeof(TreeScope));
    tree.scopes.push_back({ScopeKind::kBlock, parent});
    const auto id = statement(BlockStmt{scope, 0, 0}, origin);
    lists[id.value] = std::move(children);
    return id;
  };
  const auto scope = [&](StmtId id) {
    return std::get<BlockStmt>(tree.statements[id.value].statement).scope;
  };
  const auto expression = [&](TreeExpression value, TreeOriginId origin) {
    const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
    budget.Admit(1, sizeof(ExprNode));
    budget.Step();
    tree.expressions.push_back({std::move(value), origin});
    return id;
  };
  // Paired and shared shapes have different source sequences. Finish paired
  // edits first; shared edits then use fresh positions and batch sibling spans.
  std::vector<SequenceEdit> shared_edits;
  std::vector<std::optional<Position>> shared_positions;
  for (const bool shared_pass : {false, true}) {
    if (shared_pass) shared_positions = Positions(lists, budget);
    for (const auto& region : plan.finalizers) {
      if (region.shared_guard.has_value() != shared_pass) continue;
      budget.Step();
      const auto original = flow.tries[region.region.value];
      if (region.shared_guard) {
        const auto guard = flow.statements[region.shared_guard->value];
        if (!original || !guard || !shared_positions[original->value] ||
            !shared_positions[guard->value])
          continue;
        const auto location = *shared_positions[original->value],
                   end = *shared_positions[guard->value];
        if (location.block != end.block || location.index + 1 >= end.index)
          continue;
        const auto& following = lists[location.block.value];
        if (end.index >= following.size() ||
            following[location.index] != *original ||
            following[end.index] != *guard)
          continue;
        bool declarations = false;
        for (auto i = location.index + 1; i < end.index; ++i) {
          budget.Step();
          declarations |= std::holds_alternative<DeclarationStmt>(
              tree.statements[following[i].value].statement);
        }
        if (declarations) continue;
        const auto origin = tree.statements[original->value].origin;
        std::vector<StmtId> cleanup(following.begin() + location.index + 1,
                                    following.begin() + end.index);
        const auto enclosure = block(scope(location.block), origin, {});
        const auto body = block(scope(enclosure), origin, {*original});
        const auto finalizer =
            block(scope(enclosure), origin, std::move(cleanup));
        Reparent(tree, *original, scope(body));
        for (const auto id : lists[finalizer.value]) {
          budget.Step();
          Reparent(tree, id, scope(finalizer));
        }
        const auto outer = statement(TryStmt{body, {}, finalizer}, origin);
        lists[enclosure.value] = {outer};
        budget.Admit(1, sizeof(SequenceEdit) + sizeof(StmtId));
        shared_edits.push_back(
            {location.block, location.index, end.index, {enclosure}});
        budget.Admit(1, sizeof(FinalizerComposition));
        tree.finalizers.push_back({region.region, enclosure});
        continue;
      }
      const auto normal = Roots(region, flow, false, budget);
      const auto exceptional = Roots(region, flow, true, budget);
      const auto returning =
          region.transfer && !normal.empty() ? std::optional{normal.back()}
          : region.normal_exit ? flow.statements[region.normal_exit->value]
                               : std::nullopt;
      const auto throwing =
          region.transfer && !exceptional.empty()
              ? std::optional{exceptional.back()}
          : region.exceptional_exit
              ? flow.statements[region.exceptional_exit->value]
              : std::nullopt;
      if (!original || !returning || !throwing || !positions[original->value])
        continue;
      const auto inner =
          std::get<TryStmt>(tree.statements[original->value].statement);
      const auto handler = inner.handler->body;
      const auto location = *positions[original->value];
      const bool replaces = region.replacement.has_value();
      const bool body_tail = region.placement == CleanupPlacement::kBodyTail;
      const bool falls_through =
          !replaces && std::holds_alternative<NormalCompletion>(region.normal);
      const auto& following = lists[location.block.value];
      const auto cleanup_end = location.index + 1 + normal.size();
      // Normal completion may end this block and enter its enclosing region.
      // A preserved return/throw requires a trailing statement in the same
      // block.
      if (body_tail) {
        if (!replaces || !region.transfer || !region.cleanup.empty() ||
            normal.size() != 1 || lists[inner.body.value].empty() ||
            lists[inner.body.value].back() != normal.front() ||
            location.index + 1 != following.size())
          continue;
      } else if (normal.empty() || exceptional.empty() ||
                 cleanup_end > following.size() ||
                 (!replaces && !falls_through &&
                  cleanup_end == following.size()) ||
                 (!falls_through && following.back() != *returning) ||
                 !std::equal(normal.begin(), normal.end(),
                             following.begin() + location.index + 1))
        continue;
      std::vector<StmtId> protected_children{*original};
      if (replaces) {
        if ((!body_tail && cleanup_end != following.size()) ||
            normal.back() != *returning || exceptional.back() != *throwing)
          continue;
      } else if (!falls_through) {
        const auto copies = std::span<const StmtId>(following).subspan(
            cleanup_end, following.size() - cleanup_end - 1);
        if (!SnapshotCopies(copies, region, flow, tree, cleanup_writes, budget))
          continue;
        protected_children.insert(protected_children.end(), copies.begin(),
                                  copies.end());
        protected_children.push_back(*returning);
      }
      const auto& caught = lists[handler.value];
      const auto preserved_exit = replaces ? 0U : 1U;
      if (caught.size() < exceptional.size() + preserved_exit) continue;
      const auto prefix = caught.size() - exceptional.size() - preserved_exit;
      if (replaces) {
        if (!std::equal(exceptional.begin(), exceptional.end(),
                        caught.begin() + prefix))
          continue;
      } else if (!Tail(caught, prefix, exceptional, *throwing))
        continue;
      // Moving completion before cleanup cannot introduce a GetValue or throw.
      // The plan has separately established payload availability/identity.
      if (!replaces && !falls_through) {
        if (!LeafCompletion(tree, *returning) ||
            !LeafCompletion(tree, *throwing))
          continue;
      }
      if (falls_through &&
          !Leaf(tree,
                std::get<ThrowStmt>(tree.statements[throwing->value].statement)
                    .value))
        continue;
      std::vector<StmtId> continuation;
      if (falls_through)
        continuation.assign(following.begin() + cleanup_end, following.end());
      const auto origin = tree.statements[original->value].origin;
      const auto enclosure = block(scope(location.block), origin, {});
      const auto body =
          block(scope(enclosure), origin, std::move(protected_children));
      const auto finalizer = block(scope(enclosure), origin, {});
      const auto normal_body = block(scope(finalizer), origin, normal);
      const auto exceptional_body =
          block(scope(finalizer), origin, exceptional);
      tree.scopes[scope(inner.body).value].parent = scope(body);
      tree.scopes[scope(handler).value].parent = scope(body);
      std::u16string name;
      do {
        name = u"$completion";
        const auto digits = std::to_string(suffix++);
        name.append(digits.begin(), digits.end());
        budget.Step();
      } while (std::binary_search(names.begin(), names.end(), name));
      const NameId name_id{static_cast<std::uint32_t>(tree.names.size())};
      const SymbolId flag{static_cast<std::uint32_t>(tree.symbols.size())};
      budget.Admit(1, sizeof(TreeSymbol) + sizeof(std::u16string));
      budget.Admit(name.size(), sizeof(char16_t));
      tree.names.push_back(std::move(name));
      tree.symbols.push_back({scope(enclosure), name_id,
                              BindingKind::kCompilerTemporary,
                              DeclarationKind::kLet, NameEvidence::kSynthesized,
                              TemporaryReason::kPendingCompletion});
      const auto reset = expression(TreeLiteral{BooleanLiteral{false}}, origin);
      const auto set = expression(TreeLiteral{BooleanLiteral{true}}, origin);
      const auto assign =
          expression(AssignmentExpr{SymbolTarget{flag}, set}, origin);
      const auto condition = expression(SymbolExpr{flag}, origin);
      const auto route =
          statement(IfStmt{condition, exceptional_body, normal_body}, origin);
      lists[finalizer.value] = {route};
      const auto outer = statement(TryStmt{body, {}, finalizer}, origin);
      const TreeOriginId binding{
          static_cast<std::uint32_t>(tree.origins.size())};
      budget.Admit(1, sizeof(TreeOrigin));
      tree.origins.push_back(BindingOrigin{flag});
      const auto declaration = statement(DeclarationStmt{flag, reset}, binding);
      const auto pending = statement(ExpressionStmt{assign}, origin);
      lists[enclosure.value] = {declaration, outer};
      lists[handler.value].resize(prefix);
      lists[handler.value].push_back(pending);
      if (!replaces) lists[handler.value].push_back(*throwing);
      if (body_tail) lists[inner.body.value].pop_back();
      lists[location.block.value].resize(location.index);
      lists[location.block.value].push_back(enclosure);
      lists[location.block.value].insert(lists[location.block.value].end(),
                                         continuation.begin(),
                                         continuation.end());
      budget.Admit(1, sizeof(FinalizerComposition));
      tree.finalizers.push_back({region.region, enclosure});
    }
  }
  EditSequences(lists, shared_edits, budget);
  const auto yield_positions = Positions(lists, budget);
  const auto yield_tails = TailParents(tree, lists, budget);
  std::vector<SequenceEdit> yield_edits;
  for (const auto& route : plan.yield_returns) {
    budget.Step();
    const auto yielded = flow.statements[route.yield.value],
               branch = flow.statements[route.branch.value],
               completion = route.completion ? flow.statements[route.completion->value] : std::nullopt;
    Require(yielded && branch && (!route.completion || completion) &&
        yield_positions[yielded->value] && yield_positions[branch->value]);
    const auto location = *yield_positions[yielded->value], guard_location = *yield_positions[branch->value];
    Require(location.block == guard_location.block && location.index < guard_location.index);
    const auto* guard = std::get_if<IfStmt>(&tree.statements[branch->value].statement);
    Require(guard != nullptr);
    auto placement = YieldCleanupPlacement::kBranchArm;
    std::optional<StmtId> completion_block;
    std::vector<StmtId> cleanup;
    if (guard->alternate && !lists[guard->alternate->value].empty() &&
        (!completion || TailWithin(*guard->alternate, *completion, yield_tails, budget))) {
      auto& arm = lists[guard->alternate->value];
      if (completion && arm.back() != *completion) {
        Require(yield_positions[completion->value].has_value());
        completion_block = yield_positions[completion->value]->block;
        Require(lists[completion_block->value].back() == *completion);
        lists[completion_block->value].pop_back();
        cleanup = std::move(arm); arm = {*completion};
      } else {
        const auto end = completion ? arm.end() - 1 : arm.end();
        cleanup.assign(arm.begin(), end);
        arm.erase(arm.begin(), end);
      }
    } else {
      // An early-exiting NEXT arm can leave the RETURN corridor as the
      // following sequence. Consume that existing placement, not a different
      // control algorithm or an artificial else branch.
      Require(!guard->alternate || lists[guard->alternate->value].empty());
      Require(!completion || yield_positions[completion->value].has_value());
      // With no preserving completion the entire RETURN suffix is abrupt;
      // all its real return/throw nodes move into native finally, not just
      // their preceding effects. The inverse projection restores every root.
      auto end = completion ? *yield_positions[completion->value] :
          Position{guard_location.block, static_cast<std::uint32_t>(lists[guard_location.block.value].size())};
      if (completion && end.block != guard_location.block) {
        const auto& following = lists[guard_location.block.value];
        Require(following.size() > guard_location.index + 1 &&
            TailWithin(following.back(), *completion, yield_tails, budget));
        completion_block = end.block;
        Require(lists[completion_block->value].back() == *completion);
        lists[completion_block->value].pop_back();
        end = {guard_location.block, static_cast<std::uint32_t>(following.size())};
      }
      Require(end.block == guard_location.block && end.index > guard_location.index);
      const auto& following = lists[end.block.value];
      cleanup.assign(following.begin() + guard_location.index + 1, following.begin() + end.index);
      budget.Admit(1, sizeof(SequenceEdit));
      yield_edits.push_back({end.block, guard_location.index + 1, end.index,
          completion_block ? std::vector<StmtId>{*completion} : std::vector<StmtId>{}});
      placement = YieldCleanupPlacement::kFollowing;
    }
    // Keep the certified NEXT-only dispatch/terminal in the AST for SSA audit;
    // it is unreachable for native RETURN. Move its real cleanup roots once,
    // rather than duplicating effects or inventing access to RETURN's payload.
    const auto origin = tree.statements[yielded->value].origin;
    const auto enclosure = block(scope(location.block), origin, {});
    const auto protected_body = block(scope(enclosure), origin, {});
    const auto yield_body = block(scope(protected_body), origin, {*yielded});
    const auto catch_body = block(scope(protected_body), origin, {});
    tree.scopes[scope(catch_body).value].kind = ScopeKind::kCatch;
    const auto finalizer = block(scope(enclosure), origin, {});
    const auto cleanup_body = block(scope(finalizer), origin, std::move(cleanup));
    for (const auto child : lists[cleanup_body.value]) { budget.Step(); Reparent(tree, child, scope(cleanup_body)); }
    const auto symbol = [&](ScopeId owner, bool caught) {
      std::u16string name;
      do {
        name = caught ? u"$resumeError" : u"$return";
        const auto digits = std::to_string(suffix++); name.append(digits.begin(), digits.end()); budget.Step();
      } while (std::binary_search(names.begin(), names.end(), name));
      const NameId name_id{static_cast<std::uint32_t>(tree.names.size())};
      const SymbolId id{static_cast<std::uint32_t>(tree.symbols.size())};
      budget.Admit(1, sizeof(TreeSymbol)); budget.Admit(name.size(), sizeof(char16_t));
      tree.names.push_back(std::move(name));
      tree.symbols.push_back({owner, name_id, caught ? BindingKind::kCatchParameter : BindingKind::kCompilerTemporary,
          caught ? DeclarationKind::kCatchParameter : DeclarationKind::kLet, NameEvidence::kSynthesized,
          caught ? std::nullopt : std::optional{TemporaryReason::kPendingCompletion}});
      return id;
    };
    const auto flag = symbol(scope(enclosure), false), caught = symbol(scope(catch_body), true);
    const auto clear = [&] {
      const auto value = expression(TreeLiteral{BooleanLiteral{false}}, origin);
      return statement(ExpressionStmt{expression(AssignmentExpr{SymbolTarget{flag}, value}, origin)}, origin);
    };
    const auto caught_value = expression(SymbolExpr{caught}, origin);
    lists[catch_body.value] = {clear(), statement(ThrowStmt{caught_value}, origin)};
    const auto inner = statement(TryStmt{yield_body, CatchClause{caught, catch_body}, {}}, origin);
    lists[protected_body.value] = {inner, clear()};
    const auto condition = expression(SymbolExpr{flag}, origin);
    lists[finalizer.value] = {statement(IfStmt{condition, cleanup_body, {}}, origin)};
    const auto outer = statement(TryStmt{protected_body, {}, finalizer}, origin);
    const auto initial = expression(TreeLiteral{BooleanLiteral{true}}, origin);
    const TreeOriginId binding{static_cast<std::uint32_t>(tree.origins.size())};
    budget.Admit(1, sizeof(TreeOrigin)); tree.origins.push_back(BindingOrigin{flag});
    lists[enclosure.value] = {statement(DeclarationStmt{flag, initial}, binding), outer};
    lists[location.block.value][location.index] = enclosure;
    budget.Admit(1, sizeof(YieldReturnComposition));
    tree.yield_returns.push_back({route.yield, enclosure, *branch, completion, placement, completion_block});
  }
  EditSequences(lists, yield_edits, budget);
  if (tree.finalizers.empty() && tree.yield_returns.empty()) return;
  const auto after = Positions(lists, budget);
  for (auto& anchor : tree.anchors) {
    budget.Step();
    anchor.position = Remap(anchor.position, before, after);
  }
  tree.children.clear();
  for (std::size_t i = 0; i < lists.size(); ++i) {
    budget.Step(lists[i].size() + 1);
    if (auto* block = std::get_if<BlockStmt>(&tree.statements[i].statement)) {
      block->first_child = static_cast<std::uint32_t>(tree.children.size());
      block->child_count = static_cast<std::uint32_t>(lists[i].size());
      tree.children.insert(tree.children.end(), lists[i].begin(),
                           lists[i].end());
    }
  }
}

CompletionTreeView::CompletionTreeView(const FunctionTree& tree,
                                       const CompletionPlan& plan,
                                       TreeCheckBudget& budget)
    : tree_(tree) {
  if (tree.finalizers.empty() && tree.yield_returns.empty()) return;
  auto lists = Children(tree, budget);
  const auto before = Children(tree, budget);
  const auto positions = Positions(lists, budget);
  budget.Array(blocks_, tree.statements.size());
  budget.Array(parents_, tree.statements.size());
  budget.Admit(tree.children.size(), sizeof(StmtId));
  children_.reserve(tree.children.size());
  const auto symbol_count = tree.finalizers.size() + 2 * tree.yield_returns.size();
  budget.Admit(symbol_count, sizeof(SymbolId));
  budget.Admit(tree.finalizers.size(), sizeof(Corridor));
  symbols_.reserve(symbol_count);
  corridors_.reserve(tree.finalizers.size());
  constexpr std::size_t kRoutingExpressions =
      4;  // reset, set, assignment, condition
  constexpr std::size_t kYieldRoutingExpressions =
      7;  // initial flag, two clears/assignments, caught value, condition
  const auto expression_count = tree.finalizers.size() * kRoutingExpressions +
      tree.yield_returns.size() * kYieldRoutingExpressions;
  budget.Admit(expression_count, sizeof(ExprId));
  expressions_.reserve(expression_count);
  budget.Admit(tree.yield_returns.size(), sizeof(YieldCorridor));
  yield_routes_.reserve(tree.yield_returns.size());
  std::vector<const FinalizerRegion*> regions;
  std::size_t region_count = 0;
  for (const auto& region : plan.finalizers)
    region_count = std::max(region_count, std::size_t(region.region.value) + 1);
  budget.Array(regions, region_count);
  std::vector<std::uint8_t> consumed;
  budget.Array(consumed, region_count);
  for (const auto& region : plan.finalizers)
    regions[region.region.value] = &region;
  const auto block = [&](StmtId id) -> const BlockStmt& {
    Require(id.value < tree.statements.size());
    const auto* node =
        std::get_if<BlockStmt>(&tree.statements[id.value].statement);
    Require(node != nullptr);
    return *node;
  };
  const auto boolean = [&](ExprId id, bool value, TreeOriginId origin) {
    const auto* literal =
        std::get_if<TreeLiteral>(&tree.expressions[id.value].expression);
    Require(literal && std::holds_alternative<BooleanLiteral>(*literal) &&
            std::get<BooleanLiteral>(*literal).value == value &&
            tree.expressions[id.value].origin == origin);
  };
  std::vector<SequenceEdit> shared_edits;
  std::vector<std::optional<Position>> shared_positions;
  for (const bool shared_pass : {false, true}) {
    if (shared_pass) shared_positions = Positions(lists, budget);
    for (const auto& recovered : tree.finalizers) {
      budget.Step();
      Require(recovered.region.value < regions.size() &&
              regions[recovered.region.value]);
      const auto* region = regions[recovered.region.value];
      if (region->shared_guard.has_value() != shared_pass) continue;
      Require(!consumed[recovered.region.value]);
      consumed[recovered.region.value] = 1;
      if (region->shared_guard) {
        const auto enclosure = recovered.enclosure;
        const auto enclosure_scope = block(enclosure).scope;
        Require(shared_positions[enclosure.value].has_value() &&
                lists[enclosure.value].size() == 1);
        const auto outer_id = lists[enclosure.value].front();
        const auto* outer =
            std::get_if<TryStmt>(&tree.statements[outer_id.value].statement);
        Require(outer && !outer->handler && outer->finalizer &&
                lists[outer->body.value].size() == 1);
        const auto inner_id = lists[outer->body.value].front();
        const auto* inner =
            std::get_if<TryStmt>(&tree.statements[inner_id.value].statement);
        Require(inner && inner->handler && !inner->finalizer);
        const auto origin = tree.statements[inner_id.value].origin;
        const auto* source =
            std::get_if<TryOrigin>(&tree.origins[origin.value]);
        Require(source && source->region == recovered.region);
        const auto finalizer = *outer->finalizer;
        Require(!lists[finalizer.value].empty() &&
                tree.scopes[block(outer->body).scope.value].parent ==
                    enclosure_scope &&
                tree.scopes[block(finalizer).scope.value].parent ==
                    enclosure_scope);
        for (const auto id : {enclosure, outer_id, outer->body, finalizer})
          Require(tree.statements[id.value].origin == origin);
        const auto location = *shared_positions[enclosure.value];
        auto& parent = lists[location.block.value];
        Require(location.index + 1 < parent.size());
        const auto guard = parent[location.index + 1];
        Require(std::holds_alternative<IfStmt>(
            tree.statements[guard.value].statement));
        std::vector<StmtId> expanded{inner_id};
        expanded.insert(expanded.end(), lists[finalizer.value].begin(),
                        lists[finalizer.value].end());
        budget.Admit(1, sizeof(SequenceEdit));
        budget.Admit(expanded.size(), sizeof(StmtId));
        shared_edits.push_back({location.block, location.index,
                                location.index + 1, std::move(expanded)});
        parents_[inner_id.value] = block(outer->body).scope;
        for (const auto id : lists[finalizer.value]) {
          budget.Step();
          if (std::holds_alternative<TryStmt>(
                  tree.statements[id.value].statement))
            parents_[id.value] = block(finalizer).scope;
        }
        budget.Admit(1, sizeof(SharedCorridor));
        shared_.push_back(
            {*region->shared_guard, guard, recovered.region, inner_id});
        lists[enclosure.value].clear();
        lists[outer->body.value].clear();
        lists[finalizer.value].clear();
        continue;
      }
      const bool replaces = region->replacement.has_value();
      const bool falls_through =
          !replaces && std::holds_alternative<NormalCompletion>(region->normal);
      const auto enclosure = recovered.enclosure;
      const auto enclosure_scope = block(enclosure).scope;
      Require(positions[enclosure.value].has_value() &&
              lists[enclosure.value].size() == 2);
      const auto declaration_id = lists[enclosure.value][0];
      const auto* declaration = std::get_if<DeclarationStmt>(
          &tree.statements[declaration_id.value].statement);
      const auto outer_id = lists[enclosure.value][1];
      const auto* outer =
          std::get_if<TryStmt>(&tree.statements[outer_id.value].statement);
      Require(declaration && declaration->initializer && outer &&
              !outer->handler && outer->finalizer);
      const auto flag = declaration->symbol;
      const auto& symbol = tree.symbols[flag.value];
      Require(symbol.scope == enclosure_scope &&
              symbol.kind == BindingKind::kCompilerTemporary &&
              symbol.declaration == DeclarationKind::kLet &&
              symbol.temporary == TemporaryReason::kPendingCompletion);
      const auto body_scope = block(outer->body).scope;
      Require(replaces || falls_through ? lists[outer->body.value].size() == 1
                                        : lists[outer->body.value].size() >= 2);
      const auto inner_id = lists[outer->body.value][0];
      const auto return_id =
          replaces || falls_through
              ? std::optional<StmtId>{}
              : std::optional{lists[outer->body.value].back()};
      const auto* inner =
          std::get_if<TryStmt>(&tree.statements[inner_id.value].statement);
      Require(inner && inner->handler && !inner->finalizer);
      if (return_id) {
        Require(LeafCompletion(tree, *return_id));
      }
      const auto origin = tree.statements[inner_id.value].origin;
      const auto* source = std::get_if<TryOrigin>(&tree.origins[origin.value]);
      Require(source && source->region == recovered.region);
      const auto handler = inner->handler->body;
      const auto pending_tail = replaces ? 1U : 2U;
      Require(lists[handler.value].size() >= pending_tail);
      const auto pending_id =
          lists[handler.value][lists[handler.value].size() - pending_tail];
      const auto throw_id = replaces
                                ? std::optional<StmtId>{}
                                : std::optional{lists[handler.value].back()};
      const auto* pending = std::get_if<ExpressionStmt>(
          &tree.statements[pending_id.value].statement);
      Require(pending != nullptr);
      if (throw_id) {
        const auto* throwing =
            std::get_if<ThrowStmt>(&tree.statements[throw_id->value].statement);
        Require(throwing && Leaf(tree, throwing->value));
      }
      const auto* assignment = std::get_if<AssignmentExpr>(
          &tree.expressions[pending->expression.value].expression);
      Require(assignment &&
              std::holds_alternative<SymbolTarget>(assignment->target) &&
              std::get<SymbolTarget>(assignment->target).symbol == flag);
      const auto finalizer = *outer->finalizer;
      const auto finalizer_scope = block(finalizer).scope;
      Require(lists[finalizer.value].size() == 1);
      const auto route_id = lists[finalizer.value][0];
      const auto* route =
          std::get_if<IfStmt>(&tree.statements[route_id.value].statement);
      Require(route && route->alternate);
      const auto exceptional = route->consequent;
      const auto normal = *route->alternate;
      const auto* condition = std::get_if<SymbolExpr>(
          &tree.expressions[route->condition.value].expression);
      Require(condition && condition->symbol == flag &&
              tree.expressions[route->condition.value].origin == origin &&
              tree.expressions[pending->expression.value].origin == origin);
      boolean(*declaration->initializer, false, origin);
      boolean(assignment->value, true, origin);
      for (const auto id : {enclosure, outer->body, finalizer, normal,
                            exceptional, outer_id, pending_id, route_id})
        Require(tree.statements[id.value].origin == origin);
      Require(tree.scopes[body_scope.value].parent == enclosure_scope &&
              tree.scopes[finalizer_scope.value].parent == enclosure_scope &&
              tree.scopes[block(normal).scope.value].parent ==
                  finalizer_scope &&
              tree.scopes[block(exceptional).scope.value].parent ==
                  finalizer_scope);
      const auto location = *positions[enclosure.value];
      auto& parent = lists[location.block.value];
      Require(falls_through || location.index + 1 == parent.size());
      std::vector<StmtId> continuation(parent.begin() + location.index + 1,
                                       parent.end());
      parent.resize(location.index);
      parent.push_back(inner_id);
      if (region->placement == CleanupPlacement::kBodyTail) {
        Require(replaces && region->transfer && region->cleanup.empty() &&
                lists[normal.value].size() == 1);
        lists[inner->body.value].push_back(lists[normal.value].front());
      } else {
        parent.insert(parent.end(), lists[normal.value].begin(),
                      lists[normal.value].end());
      }
      if (return_id) {
        parent.insert(parent.end(), lists[outer->body.value].begin() + 1,
                      lists[outer->body.value].end() - 1);
        parent.push_back(*return_id);
      }
      parent.insert(parent.end(), continuation.begin(), continuation.end());
      auto& caught = lists[handler.value];
      caught.resize(caught.size() - pending_tail);
      caught.insert(caught.end(), lists[exceptional.value].begin(),
                    lists[exceptional.value].end());
      if (throw_id) caught.push_back(*throw_id);
      parents_[inner_id.value] = body_scope;
      symbols_.push_back(flag);
      expressions_.insert(expressions_.end(),
                          {*declaration->initializer, assignment->value,
                           pending->expression, route->condition});
      corridors_.push_back({region, normal, exceptional, outer->body});
      for (const auto id :
           {enclosure, outer->body, finalizer, normal, exceptional})
        lists[id.value].clear();
    }
  }
  EditSequences(lists, shared_edits, budget);
  // Reverse only the checked syntax routing around native yield. The original
  // RETURN guard/terminal remain owned nodes; restore their cleanup prefix for
  // the same SSA/edge checker, without maintaining a second source tree.
  const auto yield_positions = Positions(lists, budget);
  Require(tree.yield_returns.size() == plan.yield_returns.size());
  const auto yield_tails = TailParents(tree, lists, budget);
  std::vector<SequenceEdit> yield_edits;
  for (std::size_t i = 0; i < tree.yield_returns.size(); ++i) {
    budget.Step();
    const auto& recovered = tree.yield_returns[i]; const auto& route = plan.yield_returns[i];
    Require(recovered.enclosure.value < lists.size() && recovered.branch.value < lists.size() &&
        (!recovered.completion || recovered.completion->value < lists.size()) &&
        (!recovered.completion_block || recovered.completion_block->value < lists.size()));
    Require(recovered.yield == route.yield && yield_positions[recovered.enclosure.value] &&
        recovered.completion.has_value() == route.completion.has_value());
    const auto enclosure = recovered.enclosure;
    const auto& header = lists[enclosure.value]; Require(header.size() == 2);
    const auto* declaration = std::get_if<DeclarationStmt>(&tree.statements[header[0].value].statement);
    const auto* outer = std::get_if<TryStmt>(&tree.statements[header[1].value].statement);
    Require(declaration && declaration->initializer && outer && !outer->handler && outer->finalizer);
    const auto flag = declaration->symbol;
    Require(tree.symbols[flag.value].scope == block(enclosure).scope &&
        tree.symbols[flag.value].kind == BindingKind::kCompilerTemporary &&
        tree.symbols[flag.value].temporary == TemporaryReason::kPendingCompletion);
    const auto& body = lists[outer->body.value]; Require(body.size() == 2);
    const auto* inner = std::get_if<TryStmt>(&tree.statements[body.front().value].statement);
    Require(inner && inner->handler && !inner->finalizer && lists[inner->body.value].size() == 1);
    const auto yielded = lists[inner->body.value].front();
    const auto origin = tree.statements[yielded.value].origin;
    boolean(*declaration->initializer, true, origin);
    const auto clear = [&](StmtId id) {
      const auto* root = std::get_if<ExpressionStmt>(&tree.statements[id.value].statement); Require(root != nullptr);
      const auto* assign = std::get_if<AssignmentExpr>(&tree.expressions[root->expression.value].expression);
      Require(assign && std::holds_alternative<SymbolTarget>(assign->target) && std::get<SymbolTarget>(assign->target).symbol == flag);
      boolean(assign->value, false, origin);
      expressions_.push_back(assign->value); expressions_.push_back(root->expression);
    };
    clear(body.back());
    const auto& caught = lists[inner->handler->body.value]; Require(caught.size() == 2); clear(caught.front());
    const auto* rethrow = std::get_if<ThrowStmt>(&tree.statements[caught.back().value].statement); Require(rethrow != nullptr);
    const auto* caught_value = std::get_if<SymbolExpr>(&tree.expressions[rethrow->value.value].expression);
    Require(caught_value && caught_value->symbol == inner->handler->parameter);
    const auto& catch_symbol = tree.symbols[caught_value->symbol.value];
    Require(catch_symbol.kind == BindingKind::kCatchParameter && catch_symbol.scope == block(inner->handler->body).scope);
    const auto& finalizer = lists[outer->finalizer->value]; Require(finalizer.size() == 1);
    const auto* condition = std::get_if<IfStmt>(&tree.statements[finalizer.front().value].statement);
    Require(condition && !condition->alternate);
    const auto* test = std::get_if<SymbolExpr>(&tree.expressions[condition->condition.value].expression);
    Require(test && test->symbol == flag);
    const auto* guard = std::get_if<IfStmt>(&tree.statements[recovered.branch.value].statement);
    Require(guard && (!recovered.completion ||
        std::holds_alternative<ReturnStmt>(tree.statements[recovered.completion->value].statement)));
    const auto& cleanup = lists[condition->consequent.value];
    switch (recovered.cleanup) {
      case YieldCleanupPlacement::kBranchArm: {
        Require(guard->alternate.has_value());
        auto& arm = lists[guard->alternate->value];
        Require(recovered.completion ? arm.size() == 1 && arm.back() == *recovered.completion : arm.empty());
        if (recovered.completion_block) {
          Require(recovered.completion && TailWithin(condition->consequent, *recovered.completion_block, yield_tails, budget));
          arm.assign(cleanup.begin(), cleanup.end());
          lists[recovered.completion_block->value].push_back(*recovered.completion);
        } else arm.insert(arm.begin(), cleanup.begin(), cleanup.end());
        break;
      }
      case YieldCleanupPlacement::kFollowing: {
        Require(!guard->alternate || lists[guard->alternate->value].empty());
        Require(yield_positions[recovered.branch.value] &&
            (!recovered.completion || yield_positions[recovered.completion->value]));
        const auto branch = *yield_positions[recovered.branch.value];
        const auto end = recovered.completion ? *yield_positions[recovered.completion->value] :
            Position{branch.block, static_cast<std::uint32_t>(lists[branch.block.value].size())};
        Require(branch.block == end.block && end.index == branch.index + 1);
        budget.Admit(1, sizeof(SequenceEdit)); budget.Admit(cleanup.size(), sizeof(StmtId));
        if (recovered.completion_block) {
          Require(recovered.completion && TailWithin(condition->consequent, *recovered.completion_block, yield_tails, budget));
          lists[recovered.completion_block->value].push_back(*recovered.completion);
        }
        yield_edits.push_back({end.block, end.index, end.index + (recovered.completion_block ? 1U : 0U), cleanup});
        break;
      }
      default: Require(false);
    }
    const auto location = *yield_positions[enclosure.value]; lists[location.block.value][location.index] = yielded;
    symbols_.push_back(flag); symbols_.push_back(caught_value->symbol);
    expressions_.insert(expressions_.end(), {*declaration->initializer, rethrow->value, condition->condition});
    yield_routes_.push_back({route, yielded, recovered.branch, recovered.completion});
    for (const auto id : {enclosure, outer->body, inner->body, inner->handler->body, *outer->finalizer, condition->consequent})
      lists[id.value].clear();
  }
  EditSequences(lists, yield_edits, budget);
  const auto after = Positions(lists, budget);
  budget.Array(anchors_, tree.anchors.size());
  for (std::size_t i = 0; i < tree.anchors.size(); ++i) {
    budget.Step();
    anchors_[i] = Remap(tree.anchors[i].position, before, after);
  }
  for (std::size_t i = 0; i < lists.size(); ++i) {
    budget.Step(lists[i].size() + 1);
    if (const auto* node =
            std::get_if<BlockStmt>(&tree.statements[i].statement)) {
      blocks_[i] = {node->scope, static_cast<std::uint32_t>(children_.size()),
                    static_cast<std::uint32_t>(lists[i].size())};
      children_.insert(children_.end(), lists[i].begin(), lists[i].end());
    }
  }
}
const BlockStmt& CompletionTreeView::Block(StmtId id) const {
  return blocks_.empty()
             ? std::get<BlockStmt>(tree_.statements[id.value].statement)
             : blocks_[id.value];
}
StmtId CompletionTreeView::Child(std::uint32_t index) const {
  return blocks_.empty() ? tree_.children[index] : children_[index];
}
const AnchorPosition& CompletionTreeView::Anchor(AnchorId id) const {
  return blocks_.empty() ? tree_.anchors[id.value].position
                         : anchors_[id.value];
}
ScopeId CompletionTreeView::TryParent(StmtId id, ScopeId parent) const {
  return parents_.empty() ? parent : parents_[id.value].value_or(parent);
}
void CompletionTreeView::Finish(TreeFlow& flow, TreeCheckBudget& budget) const {
  for (const auto& route : yield_routes_) {
    budget.Step();
    Require(flow.statements[route.plan.yield.value] == route.yielded &&
        flow.statements[route.plan.branch.value] == route.branch &&
        (!route.plan.completion || flow.statements[route.plan.completion->value] == route.completion));
  }
  for (const auto& shared : shared_) {
    budget.Step();
    Require(flow.statements[shared.guard.value] == shared.branch &&
            flow.tries[shared.region.value] == shared.original);
  }
  std::vector<std::size_t> cleanup_writes;
  if (!corridors_.empty()) budget.Array(cleanup_writes, tree_.symbols.size());
  for (const auto& corridor : corridors_) {
    const auto& body =
        std::get<BlockStmt>(tree_.statements[corridor.body.value].statement);
    if (!corridor.plan->replacement && !std::holds_alternative<NormalCompletion>(corridor.plan->normal)) {
      const auto copies = std::span<const StmtId>(tree_.children).subspan(body.first_child + 1, body.child_count - 2);
      Require(SnapshotCopies(copies, *corridor.plan, flow, tree_, cleanup_writes, budget));
    }
    for (const auto exceptional : {false, true}) {
      const auto roots = Roots(*corridor.plan, flow, exceptional, budget);
      const auto block_id =
          exceptional ? corridor.exceptional : corridor.normal;
      const auto& block =
          std::get<BlockStmt>(tree_.statements[block_id.value].statement);
      Require(!roots.empty() && roots.size() == block.child_count &&
              std::equal(roots.begin(), roots.end(),
                         tree_.children.begin() + block.first_child));
    }
  }
  budget.Admit(expressions_.size(), sizeof(ExprId));
  flow.completion_expressions = expressions_;
}
}  // namespace abcd::source
