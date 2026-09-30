#include "tree_globals.h"
#include "abcd/source/module_enclosure.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
namespace {
using Error = TreeGlobalError;
void Require(bool condition, Error error) { if (!condition) throw error; }
class GlobalCheck {
 public:
  explicit GlobalCheck(analysis::SemanticBudget budget) : initial_(budget), remaining_(budget) {}
  TreeGlobals Run(const FunctionTree& tree, const GlobalScope* scope) {
    bool globals = false;
    for (const auto& node : tree.expressions) {
      Step(); globals |= std::holds_alternative<GlobalNameExpr>(node.expression) ||
          std::holds_alternative<RealmObjectExpr>(node.expression) ||
          std::holds_alternative<SymbolConstructorExpr>(node.expression) ||
          std::holds_alternative<ArgumentSnapshotExpr>(node.expression);
    }
    if (!globals) return Result();
    Require(scope != nullptr, Error::kMissingContext);
    std::vector<std::u16string_view> locals;
    Reserve(locals, tree.symbols.size());
    for (const auto& symbol : tree.symbols) {
      Step(); Require(symbol.name.value < tree.names.size(), Error::kName);
      locals.push_back(tree.names[symbol.name.value]);
    }
    std::sort(locals.begin(), locals.end(), [&](auto a, auto b) { return Less(a, b); });
    // The record-owned GlobalScope already guarantees complete sorted outer
    // indexes. Never copy/sort those indexes once per method or global read.
    for (const auto& node : tree.expressions) {
      Step();
      const auto* global = std::get_if<GlobalNameExpr>(&node.expression);
      const bool realm = std::holds_alternative<RealmObjectExpr>(node.expression);
      const bool symbol = std::holds_alternative<SymbolConstructorExpr>(node.expression);
      const bool snapshot = std::holds_alternative<ArgumentSnapshotExpr>(node.expression);
      if (!global && !realm && !symbol && !snapshot) continue;
      std::u16string_view name;
      if (realm) {
        if (scope->realm_binding) Step(scope->realm_binding->name.size());
        const auto binding = ResolveRealmBinding(tree.identity.contract, scope);
        Require(binding.has_value(), Error::kMissingContext);
        name = binding->name;
      } else if (symbol) {
        if (scope->symbol_binding) Step(scope->symbol_binding->name.size());
        const auto binding = ResolveSymbolBinding(scope);
        Require(binding.has_value(), Error::kMissingContext);
        name = *binding;
      } else if (snapshot) {
        if (scope->argument_snapshot_binding) Step(scope->argument_snapshot_binding->name.size());
        const auto binding = ResolveArgumentSnapshotBinding(scope);
        Require(binding.has_value(), Error::kMissingContext);
        name = *binding;
      } else {
        Require(global->name.value < tree.names.size(), Error::kName);
        name = tree.names[global->name.value];
      }
      Step(name.size());
      Require(realm || symbol || snapshot || AllowsGlobalReference(name, {}), Error::kName);
      Require(!Contains(std::span<const std::u16string_view>{locals}, name) &&
          !Contains(scope->enclosing_bindings, name) && !Contains(scope->record_bindings, name) && !Contains(scope->import_names, name) &&
          (!scope->module_enclosure || !Contains(scope->module_enclosure->SortedNames(), name)) &&
          !Contains(scope->parameter_bindings, name) &&
          !Equal(name, scope->class_binding) && !Equal(name, scope->self_binding) &&
          (realm || !scope->realm_binding || !Equal(name, scope->realm_binding->name)) &&
          (symbol || !scope->symbol_binding || !Equal(name, scope->symbol_binding->name)) &&
          (snapshot || !scope->argument_snapshot_binding || !Equal(name, scope->argument_snapshot_binding->name)), Error::kShadowed);
    }
    return Result();
  }
 private:
  void Step(std::uint64_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit); remaining_.work -= count;
  }
  template <class T> void Reserve(std::vector<T>& values, std::size_t count) {
    Require(count <= remaining_.bytes / sizeof(T), Error::kResourceLimit);
    remaining_.bytes -= count * sizeof(T);
    values.reserve(count);
    const auto extra = values.capacity() - count;
    Require(extra <= remaining_.bytes / sizeof(T), Error::kResourceLimit);
    remaining_.bytes -= extra * sizeof(T);
  }
  template <class A, class B> bool Less(A a, B b) {
    Step(std::min(a.size(), b.size()) + 1);
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](auto left, auto right) {
      // ASCII source enclosures can be compared with UTF-16 identifiers without
      // sign extension. Non-ASCII UTF-8 bytes cannot match admitted globals.
      return static_cast<std::make_unsigned_t<decltype(left)>>(left) <
             static_cast<std::make_unsigned_t<decltype(right)>>(right);
    });
  }
  template <class A, class B> bool Equal(A a, B b) { return !Less(a, b) && !Less(b, a); }
  template <class T> bool Contains(std::span<const T> names, std::u16string_view name) {
    const auto found = std::lower_bound(names.begin(), names.end(), name,
        [&](auto a, auto b) { return Less(a, b); });
    return found != names.end() && Equal(*found, name);
  }
  TreeGlobals Result() const { return {initial_.bytes - remaining_.bytes, initial_.work - remaining_.work}; }
  analysis::SemanticBudget initial_;
  analysis::SemanticBudget remaining_;
};
}  // namespace
std::variant<TreeGlobals, TreeGlobalError> VerifyTreeGlobals(
    const FunctionTree& tree, const GlobalScope* scope, analysis::SemanticBudget budget) {
  try { return GlobalCheck(budget).Run(tree, scope);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
