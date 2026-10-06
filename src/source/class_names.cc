#include "class_names.h"
#include "expression_children.h"
#include "tree_checks.h"
#include "abcd/source/module_enclosure.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
using Error = TreeStructureError;
struct TreeView { const FunctionTree* tree; std::size_t symbols = 0, expressions = 0; };
struct Owner { FunctionOwnerId owner; std::size_t tree; };
struct SymbolScope { std::optional<CapturedSymbol> capture; std::optional<std::size_t> depth; bool external = false; };
struct Candidate {
  const ClassTree* definition;
  std::size_t group = 0, depth = 0;
  bool named = false, active = false;
  std::optional<std::size_t> name_previous, previous, next;
};
struct EnterTree { std::size_t tree; };
struct Expression { std::size_t tree; ExprId expression; };
struct EnterClass { std::size_t candidate; std::optional<Expression> heritage; };
struct LeaveClass { std::size_t candidate; };
using Task = std::variant<EnterTree, Expression, EnterClass, LeaveClass>;
class NamePlan {
 public:
  NamePlan(const GlobalScope* scope, analysis::SemanticBudget budget) : scope_(scope), budget_(budget) {}
  ClassNames Run(const ConstructedFamily* family, const ClassTree* root_class, const FunctionTree* raw = nullptr) {
    if (family) AddFamily(*family);
    else if (raw) Grow(trees_, TreeView{raw});
    else AddClass(*root_class);
    // Each prepared owner belongs to one source creation. Inventory includes
    // descendants for lookup; lexical traversal enters them at FunctionExpr.
    for (std::size_t i = 0; i < trees_.size(); ++i) {
      const auto* tree = trees_[i].tree;
      for (const auto& node : tree->expressions) {
        budget_.Step();
        if (const auto* created = std::get_if<ClassExpr>(&node.expression)) {
          RequireTree(created->definition != nullptr, Error::kBinding);
          AddClass(*created->definition);
        }
      }
    }
    if (candidates_.empty()) return {{}, budget_.Bytes(), budget_.Work()};
    std::size_t symbols = 0, expressions = 0;
    for (std::size_t i = 0; i < trees_.size(); ++i) {
      budget_.Step(); auto& view = trees_[i];
      view.symbols = symbols; view.expressions = expressions;
      Add(symbols, view.tree->symbols.size()); Add(expressions, view.tree->expressions.size());
      Grow(owners_, Owner{view.tree->owner, i});
    }
    std::sort(owners_.begin(), owners_.end(), [&](auto a, auto b) { budget_.Step(); return a.owner < b.owner; });
    for (std::size_t i = 1; i < owners_.size(); ++i) {
      budget_.Step(); RequireTree(owners_[i-1].owner != owners_[i].owner, Error::kBinding);
    }
    budget_.Array(symbols_, symbols); budget_.Array(capture_path_, symbols);
    budget_.Array(incoming_, expressions);
    budget_.Array(entered_, trees_.size()); budget_.Array(candidate_order_, candidates_.size());
    for (std::size_t i = 0; i < candidates_.size(); ++i) candidate_order_[i] = i;
    std::sort(candidate_order_.begin(), candidate_order_.end(), [&](auto a, auto b) {
      return Less(candidates_[a].definition->name, candidates_[b].definition->name);
    });
    for (const auto i : candidate_order_) {
      const auto name = std::u16string_view{candidates_[i].definition->name};
      if (names_.empty() || Less(names_.back(), name)) Grow(names_, name);
      candidates_[i].group = names_.size() - 1;
      budget_.Step(name.size()); candidates_[i].named = IsSourceIdentifier(name);
    }
    budget_.Array(heads_, names_.size());
    for (const auto& view : trees_) {
      const auto& tree = *view.tree;
      for (const auto& capture : tree.captures) {
        budget_.Step(); RequireTree(capture.symbol.value < tree.symbols.size(), Error::kRange);
        symbols_[view.symbols + capture.symbol.value].capture = capture.target;
      }
      for (const auto& alias : tree.modules) {
        budget_.Step(); RequireTree(alias.symbol.value < tree.symbols.size(), Error::kRange);
        symbols_[view.symbols + alias.symbol.value].external = true;
      }
      for (const auto& node : tree.expressions) {
        budget_.Step(); VisitExpressionChildren(tree, node.expression, [&](ExprId child) {
          budget_.Step(); RequireTree(child.value < tree.expressions.size(), Error::kRange);
          RequireTree(!incoming_[view.expressions + child.value], Error::kOwnership);
          incoming_[view.expressions + child.value] = 1;
        });
      }
    }
    std::size_t task_count = expressions;
    Add(task_count, trees_.size()); Add(task_count, candidates_.size()); Add(task_count, 1);
    budget_.Array(tasks_, task_count);
    if (family) Push(EnterTree{Find(family->members[family->root.value].tree.owner)});
    else if (raw) Push(EnterTree{Find(raw->owner)});
    else Push(EnterClass{Find(*root_class), {}});
    while (pending_) { budget_.Step(); const auto task = tasks_[--pending_]; std::visit([&](auto at) { Walk(at); }, task); }
    ClassNames result;
    budget_.Array(result.entries, candidates_.size());
    for (std::size_t i = 0; i < candidates_.size(); ++i)
      result.entries[i] = {candidates_[i].definition, candidates_[i].named};
    std::sort(result.entries.begin(), result.entries.end(), [&](auto a, auto b) {
      budget_.Step(); return std::less<const ClassTree*>{}(a.definition, b.definition);
    });
    result.allocated_bytes = budget_.Bytes(); result.work = budget_.Work(); return result;
  }
 private:
  static void Add(std::size_t& count, std::size_t more) {
    RequireTree(more <= std::numeric_limits<std::size_t>::max() - count, Error::kResourceLimit); count += more;
  }
  template<class T> void Grow(std::vector<T>& values, T value) {
    if (values.size() == values.capacity()) {
      auto next = values.capacity(); Add(next, next ? next : 1);
      budget_.Admit(next - values.capacity(), sizeof(T)); budget_.Step(next - values.capacity());
      const auto previous = values.capacity(); values.reserve(next);
      budget_.Admit(values.capacity() - next, sizeof(T));
      RequireTree(values.capacity() > previous, Error::kResourceLimit);
    }
    values.push_back(value);
  }
  void AddFamily(const ConstructedFamily& family) {
    RequireTree(family.root.value < family.members.size(), Error::kBinding);
    for (const auto& member : family.members) { budget_.Step(); Grow(trees_, TreeView{&member.tree}); }
  }
  void AddClass(const ClassTree& tree) {
    Grow(candidates_, Candidate{&tree});
    if (tree.constructor) AddFamily(tree.constructor->Family());
    for (const auto& member : tree.members) { budget_.Step(); if (member) AddFamily(member->Family()); }
    for (const auto& definition : tree.definitions)
      for (const auto& member : definition.bodies) { budget_.Step(); if (member) AddFamily(member->Family()); }
    if (tree.initializer) Grow(trees_, TreeView{&tree.initializer->tree});
  }
  bool Less(std::u16string_view a, std::u16string_view b) {
    budget_.Step(std::min(a.size(), b.size()) + 1); return a < b;
  }
  std::optional<std::size_t> OwnerTree(FunctionOwnerId owner) {
    const auto at = std::lower_bound(owners_.begin(), owners_.end(), owner, [&](auto a, auto b) {
      budget_.Step(); return a.owner < b;
    });
    if (at == owners_.end() || at->owner != owner) return {};
    return at->tree;
  }
  std::size_t Find(FunctionOwnerId owner) {
    const auto found = OwnerTree(owner); RequireTree(found.has_value(), Error::kBinding); return *found;
  }
  std::size_t Find(const ClassTree& tree) {
    // AST class definitions have unique creation ownership. The pointer index
    // is built once below by sorting; it makes no semantic selection by order.
    if (class_order_.empty()) {
      budget_.Array(class_order_, candidates_.size());
      for (std::size_t i = 0; i < candidates_.size(); ++i) class_order_[i] = i;
      std::sort(class_order_.begin(), class_order_.end(), [&](auto a, auto b) {
        budget_.Step(); return std::less<const ClassTree*>{}(candidates_[a].definition, candidates_[b].definition);
      });
    }
    const auto at = std::lower_bound(class_order_.begin(), class_order_.end(), &tree, [&](auto i, auto p) {
      budget_.Step(); return std::less<const ClassTree*>{}(candidates_[i].definition, p);
    });
    RequireTree(at != class_order_.end() && candidates_[*at].definition == &tree, Error::kBinding); return *at;
  }
  void Push(Task task) { RequireTree(pending_ < tasks_.size(), Error::kResourceLimit); tasks_[pending_++] = task; }
  void PopName(std::size_t i) {
    auto& candidate = candidates_[i];
    RequireTree(candidate.active && heads_[candidate.group] == i, Error::kBinding);
    heads_[candidate.group] = candidate.name_previous;
    if (candidate.previous) candidates_[*candidate.previous].next = candidate.next;
    if (candidate.next) candidates_[*candidate.next].previous = candidate.previous;
    else tail_ = candidate.previous;
    candidate.active = false;
  }
  void Unknown() {
    while (tail_) { budget_.Step(); const auto i = *tail_; candidates_[i].named = false; PopName(i); }
  }
  void Reference(std::u16string_view name, std::size_t binding_depth) {
    const auto at = std::lower_bound(names_.begin(), names_.end(), name, [&](auto a, auto b) { return Less(a,b); });
    if (at == names_.end() || Less(name, *at)) return;
    auto& head = heads_[static_cast<std::size_t>(at - names_.begin())];
    // Only scopes below the real binding can shadow it. Each unsafe candidate
    // is retired once, including dense nested classes and unknown bodies.
    while (head && candidates_[*head].depth > binding_depth) {
      budget_.Step(); const auto i = *head; candidates_[i].named = false; PopName(i);
    }
  }
  std::size_t BindingDepth(std::size_t owner, SymbolId symbol) {
    auto* binding = &symbols_[trees_[owner].symbols + symbol.value];
    // Captures are aliases, not declarations in their synthetic capture scope.
    // Parent-before-child source ownership makes their target depth available;
    // memoize aliases so every capture edge is resolved at most once.
    std::size_t count = 0;
    while (!binding->depth && !binding->external) {
      budget_.Step(); RequireTree(binding->capture.has_value(), Error::kBinding);
      const auto target = *binding->capture;
      const auto parent = OwnerTree(target.owner);
      if (!parent) { binding->external = true; break; }
      RequireTree(target.symbol.value < trees_[*parent].tree->symbols.size(), Error::kRange);
      RequireTree(count < capture_path_.size(), Error::kBinding); capture_path_[count++] = binding;
      binding = &symbols_[trees_[*parent].symbols + target.symbol.value];
    }
    const auto depth = binding->external ? 0 : *binding->depth;
    while (count) capture_path_[--count]->depth = depth;
    return depth;
  }
  void Symbol(std::size_t owner, SymbolId symbol) {
    const auto& tree = *trees_[owner].tree;
    RequireTree(symbol.value < tree.symbols.size(), Error::kRange);
    RequireTree(tree.symbols[symbol.value].name.value < tree.names.size(), Error::kRange);
    Reference(tree.names[tree.symbols[symbol.value].name.value], BindingDepth(owner, symbol));
  }
  void Walk(EnterTree at) {
    RequireTree(!entered_[at.tree], Error::kOwnership); entered_[at.tree] = 1;
    const auto& view = trees_[at.tree]; const auto& tree = *view.tree;
    for (std::size_t i = 0; i < tree.symbols.size(); ++i) {
      budget_.Step(); auto& binding = symbols_[view.symbols + i];
      if (tree.symbols[i].kind == BindingKind::kModule) binding.external = true;
      if (!binding.capture && !binding.external) binding.depth = depth_;
    }
    if (!tree.recovery_issues.empty()) Unknown();
    for (std::size_t i = tree.expressions.size(); i; --i) {
      budget_.Step(); if (!incoming_[view.expressions + i - 1]) Push(Expression{at.tree, {static_cast<std::uint32_t>(i - 1)}});
    }
  }
  void Walk(Expression at) {
    const auto& tree = *trees_[at.tree].tree; const auto& expression = tree.expressions[at.expression.value].expression;
    if (const auto* created = std::get_if<ClassExpr>(&expression)) {
      Push(EnterClass{Find(*created->definition), created->heritage ? std::optional{Expression{at.tree,*created->heritage}} : std::nullopt}); return;
    }
    if (const auto* created = std::get_if<FunctionExpr>(&expression)) { Push(EnterTree{Find(created->child)}); return; }
    if (const auto* read = std::get_if<SymbolExpr>(&expression)) Symbol(at.tree, read->symbol);
    if (const auto* update = std::get_if<UpdateExpr>(&expression)) Symbol(at.tree, update->symbol);
    if (const auto* assignment = std::get_if<AssignmentExpr>(&expression))
      if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) Symbol(at.tree, target->symbol);
    if (const auto* global = std::get_if<GlobalNameExpr>(&expression)) {
      RequireTree(global->name.value < tree.names.size(), Error::kRange);
      Reference(tree.names[global->name.value], 0);
    }
    if (std::holds_alternative<RealmObjectExpr>(expression)) {
      const auto binding = ResolveRealmBinding(tree.identity.contract, scope_); if (binding) Reference(binding->name, 0); else Unknown();
    }
    if (std::holds_alternative<SymbolConstructorExpr>(expression)) {
      const auto binding = ResolveSymbolBinding(scope_); if (binding) Reference(*binding, 0); else Unknown();
    }
    if (std::holds_alternative<ArgumentSnapshotExpr>(expression)) {
      const auto binding = ResolveArgumentSnapshotBinding(scope_); if (binding) Reference(*binding, 0); else Unknown();
    }
    VisitExpressionChildren(tree, expression, [&](ExprId child) { budget_.Step(); Push(Expression{at.tree,child}); });
  }
  void Walk(EnterClass at) {
    auto& candidate = candidates_[at.candidate]; candidate.depth = ++depth_;
    if (candidate.named) {
      candidate.active = true; candidate.name_previous = heads_[candidate.group]; heads_[candidate.group] = at.candidate;
      candidate.previous = tail_; if (tail_) candidates_[*tail_].next = at.candidate; tail_ = at.candidate;
    }
    Push(LeaveClass{at.candidate});
    const auto& tree = *candidate.definition;
    if (!tree.BodiesPrepared()) Unknown();
    if (tree.initializer) Push(EnterTree{Find(tree.initializer->tree.owner)});
    for (const auto& definition : tree.definitions)
      for (const auto& member : definition.bodies) if (member) Push(EnterTree{Find(member->Family().members[member->Family().root.value].tree.owner)});
    for (const auto& member : tree.members) if (member) Push(EnterTree{Find(member->Family().members[member->Family().root.value].tree.owner)});
    if (tree.constructor) Push(EnterTree{Find(tree.constructor->Family().members[tree.constructor->Family().root.value].tree.owner)});
    if (at.heritage) Push(*at.heritage);
  }
  void Walk(LeaveClass at) { if (candidates_[at.candidate].active) PopName(at.candidate); RequireTree(depth_ != 0, Error::kScope); --depth_; }
  const GlobalScope* scope_;
  TreeCheckBudget budget_;
  std::vector<TreeView> trees_;
  std::vector<Owner> owners_;
  std::vector<Candidate> candidates_;
  std::vector<std::size_t> candidate_order_, class_order_;
  std::vector<std::u16string_view> names_;
  std::vector<std::optional<std::size_t>> heads_;
  std::vector<SymbolScope> symbols_;
  std::vector<SymbolScope*> capture_path_;
  std::vector<std::uint8_t> incoming_, entered_;
  std::vector<Task> tasks_;
  std::optional<std::size_t> tail_;
  std::size_t pending_ = 0, depth_ = 0;
};
template<class Input> std::variant<ClassNames, Error> Plan(const Input& input, const GlobalScope* scope, analysis::SemanticBudget budget) {
  try {
    if constexpr (std::is_same_v<Input, ConstructedFamily>) return NamePlan(scope,budget).Run(&input,nullptr);
    else if constexpr (std::is_same_v<Input, FunctionTree>) return NamePlan(scope,budget).Run(nullptr,nullptr,&input);
    else return NamePlan(scope,budget).Run(nullptr,&input);
  } catch (Error error) { return error; }
  catch (const std::bad_alloc&) { return Error::kResourceLimit; }
  catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
ClassNames::Use ClassNames::Lookup(const ClassTree& tree) const {
  std::uint64_t work = 1;
  const auto at = std::lower_bound(entries.begin(), entries.end(), &tree, [&](auto a, auto p) {
    ++work;
    return std::less<const ClassTree*>{}(a.definition,p);
  });
  return {at != entries.end() && at->definition == &tree && at->named, work};
}
std::variant<ClassNames, TreeStructureError> PlanClassNames(const ConstructedFamily& input, const GlobalScope* scope, analysis::SemanticBudget budget) { return Plan(input,scope,budget); }
std::variant<ClassNames, TreeStructureError> PlanClassNames(const FunctionTree& input, const GlobalScope* scope, analysis::SemanticBudget budget) { return Plan(input,scope,budget); }
std::variant<ClassNames, TreeStructureError> PlanClassNames(const ClassTree& input, const GlobalScope* scope, analysis::SemanticBudget budget) { return Plan(input,scope,budget); }
}
