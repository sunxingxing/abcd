#include "tree_print_trace.h"
#include "tree_globals.h"
#include "abcd/source/scalar_expression.h"
#include "abcd/source/module_enclosure.h"
#include "tree_structure.h"

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
          (std::holds_alternative<ArgumentSnapshotExpr>(node.expression) && std::get<ArgumentSnapshotExpr>(node.expression).form==ArgumentSnapshotForm::kExactFactory);
    }
    if (!globals) return Result();
    Require(scope != nullptr, Error::kMissingContext);
    // The record-owned GlobalScope already guarantees complete sorted outer
    // indexes. Never copy/sort those indexes once per method or global read.
    for (const auto& node : tree.expressions) {
      Step();
      const auto* global = std::get_if<GlobalNameExpr>(&node.expression);
      const bool realm = std::holds_alternative<RealmObjectExpr>(node.expression);
      const bool symbol = std::holds_alternative<SymbolConstructorExpr>(node.expression);
      const bool snapshot = (std::holds_alternative<ArgumentSnapshotExpr>(node.expression) && std::get<ArgumentSnapshotExpr>(node.expression).form==ArgumentSnapshotForm::kExactFactory);
      if (!global && !realm && !symbol && !snapshot) continue;
      std::u16string_view name;
      if (realm) {
        if (scope->realm_binding) Step(scope->realm_binding->name.size());
        const auto binding = ResolveRealmBinding(scope);
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
      Require(realm || symbol || snapshot || IsSourceIdentifier(name), Error::kName);
      Require(!Contains(scope->enclosing_bindings, name) && !Contains(scope->record_bindings, name) && !Contains(scope->import_names, name) &&
          (!scope->module_enclosure || !Contains(scope->module_enclosure->SortedNames(), name)) &&
          !Contains(scope->parameter_bindings, name) &&
          !Equal(name, scope->class_binding) && !Equal(name, scope->self_binding) &&
          (realm || !scope->realm_binding || !Equal(name, scope->realm_binding->name)) &&
          (symbol || !scope->symbol_binding || !Equal(name, scope->symbol_binding->name)) &&
          (snapshot || !scope->argument_snapshot_binding || !Equal(name, scope->argument_snapshot_binding->name)), Error::kShadowed);
    }
    // The shared iterative traversal owns syntax occurrences and active lexical
    // scope. Recheck at publication rather than trusting a stale scope report.
    const auto structure = VerifyTreeStructure(tree, remaining_, scope);
    const auto* checked = std::get_if<TreeStructure>(&structure);
    if (!checked) {
      switch (std::get<TreeStructureError>(structure)) {
        case TreeStructureError::kGlobalShadowed: throw Error::kShadowed;
        case TreeStructureError::kResourceLimit: throw Error::kResourceLimit;
        case TreeStructureError::kWorkLimit: throw Error::kWorkLimit;
        case TreeStructureError::kRange: case TreeStructureError::kOwnership:
        case TreeStructureError::kScope: case TreeStructureError::kBinding:
        case TreeStructureError::kName: case TreeStructureError::kTarget:
        case TreeStructureError::kOrigin: case TreeStructureError::kUnsupportedSyntax:
        case TreeStructureError::kRegExpSyntax: case TreeStructureError::kRegExpUnproven:
        case TreeStructureError::kRegExpSource: case TreeStructureError::kContract:
          throw Error::kStructure;
      }
    }
    Step(checked->work); remaining_.bytes -= checked->allocated_bytes;
    return Result();
  }
 private:
  void Step(std::uint64_t count = 1) {
    if(count>remaining_.work)PrintCharge(TreePrintBudgetKind::kWork,count,remaining_.work,initial_.work); Require(count <= remaining_.work, Error::kWorkLimit); remaining_.work -= count;
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
  } catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return Error::kResourceLimit;
  } catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return Error::kResourceLimit; }
}
}  // namespace abcd::source
