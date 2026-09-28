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
  budget.Admit(region.cleanup.size(), sizeof(StmtId));
  roots.reserve(region.cleanup.size());
  for (const auto& pair : region.cleanup) {
    budget.Step();
    const auto op = exceptional ? pair.exceptional : pair.normal;
    if (flow.statements[op.value]) roots.push_back(*flow.statements[op.value]);
  }
  return roots;
}
bool Leaf(const FunctionTree& tree, ExprId id) {
  const auto& expression = tree.expressions[id.value].expression;
  return std::holds_alternative<SymbolExpr>(expression) ||
         std::holds_alternative<TreeLiteral>(expression);
}
bool Tail(const std::vector<StmtId>& children, std::size_t first,
          const std::vector<StmtId>& cleanup, StmtId exit) {
  return first + cleanup.size() + 1 == children.size() &&
         children.back() == exit &&
         std::equal(cleanup.begin(), cleanup.end(), children.begin() + first);
}
}  // namespace

void RecoverFinalizers(const CompletionPlan& plan, const TreeFlow& flow,
                       FunctionTree& tree, TreeCheckBudget& budget) {
  if (plan.finalizers.empty()) return;
  Require(tree.finalizers.empty());
  const auto before = Children(tree, budget);
  auto lists = Children(tree, budget);
  const auto positions = Positions(before, budget);
  // Reserve before taking views: moving an SSO string invalidates its view.
  // Monotone suffixes keep generated names unique; only input names need an
  // index. Sorted lookup has no attacker-controlled hash collision behavior.
  budget.Admit(tree.names.size() + plan.finalizers.size(),
               sizeof(std::u16string));
  tree.names.reserve(tree.names.size() + plan.finalizers.size());
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
  for (const auto& region : plan.finalizers) {
    budget.Step();
    const auto original = flow.tries[region.region.value];
    const auto returning = flow.statements[region.normal_exit.value];
    const auto throwing = flow.statements[region.exceptional_exit.value];
    if (!original || !returning || !throwing || !positions[original->value])
      continue;
    const auto inner =
        std::get<TryStmt>(tree.statements[original->value].statement);
    const auto handler = inner.handler->body;
    const auto location = *positions[original->value];
    const auto normal = Roots(region, flow, false, budget);
    const auto exceptional = Roots(region, flow, true, budget);
    if (normal.empty() || exceptional.empty() ||
        !Tail(lists[location.block.value], location.index + 1, normal,
              *returning))
      continue;
    const auto& caught = lists[handler.value];
    if (caught.size() < exceptional.size() + 1) continue;
    const auto prefix = caught.size() - exceptional.size() - 1;
    if (!Tail(caught, prefix, exceptional, *throwing)) continue;
    const auto& returned =
        std::get<ReturnStmt>(tree.statements[returning->value].statement);
    const auto& thrown =
        std::get<ThrowStmt>(tree.statements[throwing->value].statement);
    // Moving completion before cleanup cannot introduce a GetValue or throw.
    // The plan has separately established payload availability/identity.
    if ((returned.value && !Leaf(tree, *returned.value)) ||
        !Leaf(tree, thrown.value))
      continue;
    const auto origin = tree.statements[original->value].origin;
    const auto enclosure = block(scope(location.block), origin, {});
    const auto body = block(scope(enclosure), origin, {*original, *returning});
    const auto finalizer = block(scope(enclosure), origin, {});
    const auto normal_body = block(scope(finalizer), origin, normal);
    const auto exceptional_body = block(scope(finalizer), origin, exceptional);
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
    const TreeOriginId binding{static_cast<std::uint32_t>(tree.origins.size())};
    budget.Admit(1, sizeof(TreeOrigin));
    tree.origins.push_back(BindingOrigin{flag});
    const auto declaration = statement(DeclarationStmt{flag, reset}, binding);
    const auto pending = statement(ExpressionStmt{assign}, origin);
    lists[enclosure.value] = {declaration, outer};
    lists[handler.value].resize(prefix);
    lists[handler.value].push_back(pending);
    lists[handler.value].push_back(*throwing);
    lists[location.block.value].resize(location.index);
    lists[location.block.value].push_back(enclosure);
    budget.Admit(1, sizeof(FinalizerComposition));
    tree.finalizers.push_back({region.region, enclosure});
  }
  if (tree.finalizers.empty()) return;
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
  if (tree.finalizers.empty()) return;
  auto lists = Children(tree, budget);
  const auto before = Children(tree, budget);
  const auto positions = Positions(lists, budget);
  budget.Array(blocks_, tree.statements.size());
  budget.Array(parents_, tree.statements.size());
  budget.Admit(tree.children.size(), sizeof(StmtId));
  children_.reserve(tree.children.size());
  budget.Admit(tree.finalizers.size(), sizeof(SymbolId) + sizeof(Corridor));
  symbols_.reserve(tree.finalizers.size());
  corridors_.reserve(tree.finalizers.size());
  constexpr std::size_t kRoutingExpressions =
      4;  // reset, set, assignment, condition
  budget.Admit(tree.finalizers.size(), kRoutingExpressions * sizeof(ExprId));
  expressions_.reserve(tree.finalizers.size() * kRoutingExpressions);
  std::vector<const FinalizerRegion*> regions;
  std::size_t region_count = 0;
  for (const auto& region : plan.finalizers)
    region_count = std::max(region_count, std::size_t(region.region.value) + 1);
  budget.Array(regions, region_count);
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
  for (const auto& recovered : tree.finalizers) {
    budget.Step();
    Require(recovered.region.value < regions.size() &&
            regions[recovered.region.value]);
    const auto* region = regions[recovered.region.value];
    regions[recovered.region.value] = nullptr;
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
    Require(lists[outer->body.value].size() == 2);
    const auto inner_id = lists[outer->body.value][0];
    const auto return_id = lists[outer->body.value][1];
    const auto* inner =
        std::get_if<TryStmt>(&tree.statements[inner_id.value].statement);
    const auto* returning =
        std::get_if<ReturnStmt>(&tree.statements[return_id.value].statement);
    Require(inner && inner->handler && !inner->finalizer && returning &&
            (!returning->value || Leaf(tree, *returning->value)));
    const auto origin = tree.statements[inner_id.value].origin;
    const auto* source = std::get_if<TryOrigin>(&tree.origins[origin.value]);
    Require(source && source->region == recovered.region);
    const auto handler = inner->handler->body;
    Require(lists[handler.value].size() >= 2);
    const auto pending_id =
        lists[handler.value][lists[handler.value].size() - 2];
    const auto throw_id = lists[handler.value].back();
    const auto* pending = std::get_if<ExpressionStmt>(
        &tree.statements[pending_id.value].statement);
    const auto* throwing =
        std::get_if<ThrowStmt>(&tree.statements[throw_id.value].statement);
    Require(pending && throwing && Leaf(tree, throwing->value));
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
            tree.scopes[block(normal).scope.value].parent == finalizer_scope &&
            tree.scopes[block(exceptional).scope.value].parent ==
                finalizer_scope);
    const auto location = *positions[enclosure.value];
    auto& parent = lists[location.block.value];
    Require(location.index + 1 == parent.size());
    parent.pop_back();
    parent.push_back(inner_id);
    parent.insert(parent.end(), lists[normal.value].begin(),
                  lists[normal.value].end());
    parent.push_back(return_id);
    auto& caught = lists[handler.value];
    caught.resize(caught.size() - 2);
    caught.insert(caught.end(), lists[exceptional.value].begin(),
                  lists[exceptional.value].end());
    caught.push_back(throw_id);
    parents_[inner_id.value] = body_scope;
    symbols_.push_back(flag);
    expressions_.insert(expressions_.end(),
                        {*declaration->initializer, assignment->value,
                         pending->expression, route->condition});
    corridors_.push_back({region, normal, exceptional});
    for (const auto id :
         {enclosure, outer->body, finalizer, normal, exceptional})
      lists[id.value].clear();
  }
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
  for (const auto& corridor : corridors_) {
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
