#include "family_enclosure.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "tree_structure.h"

namespace abcd::source {
namespace {
using Error = FamilyEnclosureError;
struct ScopeInterval { std::optional<std::size_t> begin; std::size_t end = 0; };
struct BindingInterval { std::u16string_view name; std::size_t begin; std::size_t end; };
struct ScopeCursor { std::size_t scope; std::size_t next; };
enum class EnclosureExtent : std::uint8_t { kFamily, kDestination };
class EnclosureCheck {
 public:
  EnclosureCheck(const ConstructedFamily& family, analysis::SemanticBudget budget,
      EnclosureExtent extent = EnclosureExtent::kFamily, const GlobalScope* scope = nullptr)
      : family_(family), initial_(budget), left_(budget), extent_(extent), scope_(scope) {}
  FamilyEnclosure Run() {
    Admit(1, sizeof(EnclosureCheck));
    const auto count = family_.members.size();
    Require(count && family_.root.value < count && family_.parents.size() == count, Error::kLayout);
    Require(count <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Array(structures_, count); Array(offsets_, count);
    std::size_t scopes = 0, bindings = 0;
    for (std::size_t i = 0; i < count; ++i) {
      member_ = FamilyId{static_cast<std::uint32_t>(i)}; Step();
      const auto& tree = family_.members[i].tree;
      auto checked = VerifyTreeStructure(tree, left_);
      if (const auto* error = std::get_if<TreeStructureError>(&checked)) {
        switch (*error) {
          case TreeStructureError::kResourceLimit: Fail(Error::kResourceLimit);
          case TreeStructureError::kWorkLimit: Fail(Error::kWorkLimit);
          default: Fail(Error::kStructure);
        }
      }
      auto structure = std::get<TreeStructure>(std::move(checked));
      Admit(structure.allocated_bytes, 1); Step(structure.work);
      structures_[i] = std::move(structure);
      offsets_[i] = scopes;
      Add(scopes, tree.scopes.size()); Add(bindings, tree.symbols.size());
    }
    Array(parents_, scopes); Array(intervals_, scopes);
    std::optional<std::size_t> root;
    for (std::size_t i = 0; i < count; ++i) {
      member_ = FamilyId{static_cast<std::uint32_t>(i)};
      const auto& tree = family_.members[i].tree;
      std::optional<std::size_t> enclosing;
      if (i == family_.root.value) Require(!family_.parents[i], Error::kLayout);
      else {
        const auto& parent = family_.parents[i];
        Require(parent && parent->function.value < count, Error::kLayout);
        const auto p = parent->function.value;
        Require(parent->expression.value < structures_[p].expression_scopes.size(), Error::kLayout);
        const auto* creation = std::get_if<FunctionExpr>(
            &family_.members[p].tree.expressions[parent->expression.value].expression);
        Require(creation && creation->child == tree.owner, Error::kLayout);
        enclosing = offsets_[p] + structures_[p].expression_scopes[parent->expression.value].value;
      }
      for (std::size_t s = 0; s < tree.scopes.size(); ++s) {
        Step();
        const auto parent = tree.scopes[s].parent;
        parents_[offsets_[i] + s] = parent ? std::optional{offsets_[i] + parent->value} : enclosing;
        if (!parents_[offsets_[i] + s]) {
          Require(!root, Error::kLayout); root = offsets_[i] + s;
        }
      }
    }
    Require(root.has_value(), Error::kLayout);
    WalkScopes(*root);
    Array(bindings_, bindings);
    std::size_t next = 0;
    for (std::size_t i = 0; i < count; ++i) {
      member_ = FamilyId{static_cast<std::uint32_t>(i)};
      const auto& tree = family_.members[i].tree;
      for (const auto& symbol : tree.symbols) {
        Step(); const auto& interval = intervals_[offsets_[i] + symbol.scope.value];
        bindings_[next++] = {tree.names[symbol.name.value], *interval.begin, interval.end};
      }
    }
    std::sort(bindings_.begin(), bindings_.end(), [&](const auto& a, const auto& b) {
      const auto order = Compare(a.name, b.name);
      return order < 0 || (order == 0 && a.begin < b.begin);
    });
    // In-place union of same-name lexical intervals. Only ancestor overlaps
    // merge; disjoint sibling blocks remain disjoint. No ancestry per query.
    next = 0;
    for (const auto binding : bindings_) {
      Step();
      if (next && Compare(bindings_[next - 1].name, binding.name) == 0 &&
          binding.begin <= bindings_[next - 1].end)
        bindings_[next - 1].end = std::max(bindings_[next - 1].end, binding.end);
      else bindings_[next++] = binding;
    }
    bindings_.resize(next);
    std::size_t globals = 0;
    for (std::size_t i = 0; i < count; ++i) {
      member_ = FamilyId{static_cast<std::uint32_t>(i)};
      const auto& tree = family_.members[i].tree;
      for (std::size_t e = 0; e < tree.expressions.size(); ++e) {
        Step(); const auto* global = std::get_if<GlobalNameExpr>(&tree.expressions[e].expression);
        if (!global) continue;
        name_ = global->name; ++globals;
        const auto point = *intervals_[offsets_[i] + structures_[i].expression_scopes[e].value].begin;
        const auto name = std::u16string_view{tree.names[global->name.value]};
        if (extent_ == EnclosureExtent::kDestination) Destination(name);
        const auto found = std::upper_bound(bindings_.begin(), bindings_.end(), point,
            [&](std::size_t at, const BindingInterval& binding) {
              const auto order = Compare(name, binding.name);
              return order < 0 || (order == 0 && at < binding.begin);
            });
        if (found != bindings_.begin()) {
          const auto& before = *(found - 1);
          Require(Compare(name, before.name) != 0 || point >= before.end, Error::kShadowedGlobal);
        }
        name_.reset();
      }
    }
    return {globals, initial_.bytes - left_.bytes, initial_.work - left_.work};
  }
 private:
  template<class A, class B> bool Less(A a, B b) {
    Step(std::min(a.size(), b.size()) + 1);
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](auto left, auto right) {
      return static_cast<std::make_unsigned_t<decltype(left)>>(left) <
          static_cast<std::make_unsigned_t<decltype(right)>>(right);
    });
  }
  template<class A, class B> bool Equal(A a, B b) { return !Less(a, b) && !Less(b, a); }
  bool Contains(std::span<const std::string_view> names, std::u16string_view name) {
    const auto found = std::lower_bound(names.begin(), names.end(), name,
        [&](auto a, auto b) { return Less(a, b); });
    return found != names.end() && Equal(*found, name);
  }
  void Destination(std::u16string_view name) {
    Require(scope_ != nullptr, Error::kMissingContext);
    // VerifyTreeStructure already enforced the shared global-name policy.
    // Borrow record-owned sorted indexes, not a copied union per descendant.
    // Family-local lexical ranges were checked separately at creation scopes.
    Require(!Contains(scope_->enclosing_bindings, name) && !Contains(scope_->import_names, name) &&
        !Contains(scope_->parameter_bindings, name) && !Equal(name, scope_->class_binding) &&
        !Equal(name, scope_->self_binding) &&
        (!scope_->realm_binding || !Equal(name, scope_->realm_binding->name)), Error::kShadowedGlobal);
  }
  void WalkScopes(std::size_t root) {
    const auto count = parents_.size();
    Require(count < std::numeric_limits<std::size_t>::max(), Error::kResourceLimit);
    std::vector<std::size_t> starts, cursors, children;
    std::vector<ScopeCursor> stack;
    Array(starts, count + 1); Array(cursors, count); Array(children, count - 1); Array(stack, count);
    for (const auto parent : parents_) {
      Step(); if (parent) { Require(*parent < count, Error::kLayout); ++starts[*parent + 1]; }
    }
    for (std::size_t i = 0; i < count; ++i) {
      Step(); starts[i + 1] += starts[i]; cursors[i] = starts[i];
    }
    Require(starts.back() == children.size(), Error::kLayout);
    for (std::size_t i = 0; i < count; ++i) {
      Step(); if (parents_[i]) children[cursors[*parents_[i]]++] = i;
    }
    std::size_t clock = 0, depth = 1;
    intervals_[root].begin = clock++; stack[0] = {root, starts[root]};
    while (depth) {
      Step(); auto& frame = stack[depth - 1];
      if (frame.next == starts[frame.scope + 1]) {
        intervals_[frame.scope].end = clock; --depth; continue;
      }
      const auto child = children[frame.next++];
      Require(!intervals_[child].begin && depth < count, Error::kLayout);
      intervals_[child].begin = clock++; stack[depth++] = {child, starts[child]};
    }
    Require(clock == count, Error::kLayout);
  }
  int Compare(std::u16string_view a, std::u16string_view b) {
    Step(std::min(a.size(), b.size()) + 1); return a.compare(b);
  }
  void Add(std::size_t& value, std::size_t count) {
    Require(count <= std::numeric_limits<std::size_t>::max() - value, Error::kResourceLimit); value += count;
  }
  void Step(std::uint64_t count = 1) { Require(count <= left_.work, Error::kWorkLimit); left_.work -= count; }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= count * width;
  }
  template<class T> void Array(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Step(count); values.resize(count); Admit(values.capacity() - count, sizeof(T));
  }
  [[noreturn]] void Fail(Error error) const { throw FamilyEnclosureFailure{error, member_, name_}; }
  void Require(bool ok, Error error) const { if (!ok) Fail(error); }
  const ConstructedFamily& family_;
  analysis::SemanticBudget initial_, left_;
  EnclosureExtent extent_;
  const GlobalScope* scope_;
  FamilyId member_{0};
  std::optional<NameId> name_;
  std::vector<TreeStructure> structures_;
  std::vector<std::size_t> offsets_;
  std::vector<std::optional<std::size_t>> parents_;
  std::vector<ScopeInterval> intervals_;
  std::vector<BindingInterval> bindings_;
};
}  // namespace
std::variant<FamilyEnclosure, FamilyEnclosureFailure> VerifyFamilyEnclosure(
    const ConstructedFamily& family, analysis::SemanticBudget budget) {
  try { return EnclosureCheck(family, budget).Run();
  } catch (const FamilyEnclosureFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyEnclosureFailure{Error::kResourceLimit, {0}, {}};
  } catch (const std::length_error&) { return FamilyEnclosureFailure{Error::kResourceLimit, {0}, {}}; }
}
std::variant<FamilyEnclosure, FamilyEnclosureFailure> VerifyFamilyDestination(
    const ConstructedFamily& family, const GlobalScope* scope, analysis::SemanticBudget budget) {
  try { return EnclosureCheck(family, budget, EnclosureExtent::kDestination, scope).Run();
  } catch (const FamilyEnclosureFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyEnclosureFailure{Error::kResourceLimit, {0}, {}};
  } catch (const std::length_error&) { return FamilyEnclosureFailure{Error::kResourceLimit, {0}, {}}; }
}
}  // namespace abcd::source
