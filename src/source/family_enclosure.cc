#include "tree_print_trace.h"
#include "family_enclosure.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "tree_structure.h"
#include "abcd/source/module_enclosure.h"

namespace abcd::source {
namespace {
using Error = FamilyEnclosureError;
struct ScopeInterval { std::optional<std::size_t> begin; std::size_t end = 0; };
struct BindingInterval { std::u16string_view name; std::size_t begin; std::size_t end; };
struct ModuleName { std::u16string_view name; ModuleSymbol target; FamilyId member; NameId id; };
struct ScopeCursor { std::size_t scope; std::size_t next; };
enum class EnclosureExtent : std::uint8_t { kFamily, kDestination };
enum class BindingTarget : std::uint8_t { kGlobal, kRealm, kSymbolConstructor, kArgumentSnapshot };
class EnclosureCheck {
 public:
  EnclosureCheck(const ConstructedFamily& family, analysis::SemanticBudget budget,
      EnclosureExtent extent = EnclosureExtent::kFamily, const GlobalScope* scope = nullptr, ModuleRole root_role = ModuleRole::kReader)
      : family_(family), initial_(budget), left_(budget), extent_(extent), scope_(scope), root_role_(root_role) {}
  FamilyEnclosure Run() {
    Admit(1, sizeof(EnclosureCheck));
    const auto count = family_.members.size();
    Require(count && family_.root.value < count && family_.parents.size() == count, Error::kLayout);
    Require(count <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Array(structures_, count); Array(offsets_, count);
    std::size_t scopes = 0, bindings = 0, modules = 0;
    for (std::size_t i = 0; i < count; ++i) {
      member_ = FamilyId{static_cast<std::uint32_t>(i)}; Step();
      const auto& tree = family_.members[i].tree;
      auto checked = VerifyTreeStructure(tree, left_, extent_ == EnclosureExtent::kDestination ? scope_ : nullptr);
      if (const auto* error = std::get_if<TreeStructureError>(&checked)) {
        switch (*error) {
          case TreeStructureError::kResourceLimit: Fail(Error::kResourceLimit);
          case TreeStructureError::kWorkLimit: Fail(Error::kWorkLimit);
          default: Fail(Error::kStructure);
        }
      }
      auto structure = std::get<TreeStructure>(std::move(checked));
      // VerifyTreeStructure has destroyed its checker. The returned scopes
      // remain in structures_, so only dead scratch is credited here.
      Admit(structure.allocated_bytes, 1); Step(structure.work);
      left_.bytes+=structure.allocated_bytes-structure.retained_bytes-structure.input_bytes;
      input_bytes_+=structure.input_bytes;
      structures_[i] = std::move(structure);
      if (extent_ == EnclosureExtent::kDestination && (!tree.modules.empty() || tree.module_scope)) {
        Require(family_.members[i].record.has_value() && scope_ && scope_->module_enclosure,
            Error::kMissingModuleContext);
        const auto destination = VerifyModuleDestination(tree, *family_.members[i].record, scope_, left_,
            i == family_.root.value ? root_role_ : ModuleRole::kReader);
        if (const auto* error = std::get_if<ModuleEnclosureError>(&destination)) {
          if (*error == ModuleEnclosureError::kResourceLimit) Fail(Error::kResourceLimit);
          if (*error == ModuleEnclosureError::kWorkLimit) Fail(Error::kWorkLimit);
          Fail(Error::kModuleBinding);
        }
        Step(std::get<ModuleDestination>(destination).work);
      }
      offsets_[i] = scopes;
      Add(scopes, tree.scopes.size()); Add(bindings, tree.symbols.size());
      Add(modules, tree.modules.size());
    }
    // Only new property-naming obligations enter this proof. Original function
    // naming plans retain their creation names. File-backed creation contexts
    // have already checked inferred_name against RuntimeName; an empty name and
    // absent self binding are required, not a spelling guessed from an opcode.
    bool naming = false;
    for (const auto& member : family_.members) for (const auto& property : member.tree.properties) {
      Step(); naming |= property.name_setting == analysis::PropertyNameSetting::kPropertyKey;
    }
    if (naming) {
      std::vector<std::pair<FunctionOwnerId, std::size_t>> owners;
      Array(owners, count);
      for (std::size_t i = 0; i < count; ++i) { Step(); owners[i] = {family_.members[i].tree.owner, i}; }
      std::sort(owners.begin(), owners.end(), [&](const auto& a, const auto& b) { Step(); return a.first < b.first; });
      for (std::size_t i = 0; i < count; ++i) {
        member_ = FamilyId{static_cast<std::uint32_t>(i)};
        const auto& tree = family_.members[i].tree;
        for (const auto& property : tree.properties) {
          Step(); if (property.name_setting != analysis::PropertyNameSetting::kPropertyKey) continue;
          const auto* function = std::get_if<FunctionExpr>(&tree.expressions[property.value.value].expression);
          if (!function) continue; // Class anonymity/static timing checked by TreeStructure.
          const auto child = std::lower_bound(owners.begin(), owners.end(), function->child,
              [&](const auto& a, FunctionOwnerId b) { Step(); return a.first < b; });
          Require(child != owners.end() && child->first == function->child, Error::kStructure);
          const auto& target = family_.members[child->second].tree;
          Require(!target.self && target.inferred_name &&
              target.names[target.inferred_name->value].empty() &&
              target.invocation == InvocationForm::kDeclared, Error::kStructure);
        }
      }
    }
    Array(parents_, scopes); Array(intervals_, scopes);
    std::optional<std::size_t> root;
    for (std::size_t i = 0; i < count; ++i) {
      member_ = FamilyId{static_cast<std::uint32_t>(i)};
      const auto& tree = family_.members[i].tree;
      if (extent_ == EnclosureExtent::kDestination && tree.invocation == InvocationForm::kArrowMethod)
        Require(CallableText(tree.identity.contract) == CallableTextPolicy::kRecovered ||
            (scope_ && scope_->callable_text == CallableTextPolicy::kRecovered), Error::kCallableText);
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
    Require(modules <= bindings, Error::kStructure);
    Array(bindings_, bindings); Array(locals_, modules ? bindings - modules : 0); Array(modules_, modules);
    std::size_t next = 0, local = 0, module = 0;
    for (std::size_t i = 0; i < count; ++i) {
      member_ = FamilyId{static_cast<std::uint32_t>(i)};
      const auto& tree = family_.members[i].tree;
      // Module-owned locals are declarations of the same binding referenced by
      // descendant module aliases, not unrelated lexical shadowing. Classify by
      // the checked alias table, not BindingKind: local aliases use kLocal.
      std::vector<std::uint8_t> module_symbols;
      Array(module_symbols, tree.symbols.size());
      for (const auto& alias : tree.modules) { Step(); module_symbols[alias.symbol.value] = 1; }
      std::size_t symbol_index = 0;
      for (const auto& symbol : tree.symbols) {
        Step(); const auto& interval = intervals_[offsets_[i] + symbol.scope.value];
        bindings_[next++] = {tree.names[symbol.name.value], *interval.begin, interval.end};
        if (modules && !module_symbols[symbol_index]) {
          Require(local < locals_.size(), Error::kStructure);
          locals_[local++] = bindings_[next - 1];
        }
        ++symbol_index;
      }
      for (const auto& alias : tree.modules) {
        Step(); const auto& symbol = tree.symbols[alias.symbol.value];
        modules_[module++] = {tree.names[symbol.name.value], alias.target, member_, symbol.name};
      }
    }
    locals_.resize(local);
    Merge(bindings_); Merge(locals_); CheckModuleNames();
    std::size_t globals = 0, module_reads = 0, realm_reads = 0, symbol_reads = 0;
    std::size_t argument_snapshots = 0;
    for (std::size_t i = 0; i < count; ++i) {
      member_ = FamilyId{static_cast<std::uint32_t>(i)};
      const auto& tree = family_.members[i].tree;
      for (std::size_t e = 0; e < tree.expressions.size(); ++e) {
        Step(); const auto& expression = tree.expressions[e].expression;
        const auto* global = std::get_if<GlobalNameExpr>(&expression);
        const auto* reference = std::get_if<SymbolExpr>(&expression);
        const auto point = *intervals_[offsets_[i] + structures_[i].expression_scopes[e].value].begin;
        if (std::holds_alternative<RealmObjectExpr>(expression)) {
          ++realm_reads;
          // Internal family construction does not know the final host binding.
          // Publication must resolve it and repeat the full ancestry check.
          if (extent_ == EnclosureExtent::kDestination) {
            if (scope_ && scope_->realm_binding) Step(scope_->realm_binding->name.size());
            const auto binding = ResolveRealmBinding(tree.identity.contract, scope_);
            Require(binding.has_value(), Error::kMissingContext);
            Destination(binding->name, BindingTarget::kRealm);
            Require(!Shadows(bindings_, binding->name, point), Error::kShadowedGlobal);
          }
          continue;
        }
        if (std::holds_alternative<SymbolConstructorExpr>(expression)) {
          ++symbol_reads;
          if (extent_ == EnclosureExtent::kDestination) {
            if (scope_ && scope_->symbol_binding) Step(scope_->symbol_binding->name.size());
            const auto binding = ResolveSymbolBinding(scope_);
            Require(binding.has_value(), Error::kMissingContext);
            Destination(*binding, BindingTarget::kSymbolConstructor);
            Require(!Shadows(bindings_, *binding, point), Error::kShadowedGlobal);
          }
          continue;
        }
        if (std::holds_alternative<ArgumentSnapshotExpr>(expression)) {
          ++argument_snapshots;
          if (extent_ == EnclosureExtent::kDestination) {
            if (scope_ && scope_->argument_snapshot_binding) Step(scope_->argument_snapshot_binding->name.size());
            const auto binding = ResolveArgumentSnapshotBinding(scope_);
            Require(binding.has_value(), Error::kMissingContext);
            Destination(*binding, BindingTarget::kArgumentSnapshot);
            Require(!Shadows(bindings_, *binding, point), Error::kShadowedGlobal);
          }
          continue;
        }
        const bool module_read = reference && tree.symbols[reference->symbol.value].kind == BindingKind::kModule;
        if (!global && !module_read) continue;
        name_ = global ? global->name : tree.symbols[reference->symbol.value].name;
        const auto name = std::u16string_view{tree.names[name_->value]};
        if (global) {
          ++globals;
          if (extent_ == EnclosureExtent::kDestination) Destination(name);
          Require(!Shadows(bindings_, name, point), Error::kShadowedGlobal);
        } else {
          ++module_reads;
          // Module-scope symbols are aliases to the record, not declarations
          // inside this child that could hide a parent local or its TDZ.
          Require(!Shadows(locals_, name, point), Error::kShadowedModule);
          // Native binding checks ran once per member above. This independent
          // ancestry check still rejects parent locals/TDZ hiding the alias.
        }
        name_.reset();
      }
    }
    return {globals, peak_, initial_.work - left_.work, module_reads, realm_reads, symbol_reads, argument_snapshots, input_bytes_};
  }
 private:
  void Merge(std::vector<BindingInterval>& bindings) {
    std::sort(bindings.begin(), bindings.end(), [&](const auto& a, const auto& b) {
      const auto order = Compare(a.name, b.name);
      return order < 0 || (order == 0 && a.begin < b.begin);
    });
    // In-place union preserves disjoint sibling scopes. Both indexes reuse the
    // same Euler intervals; no per-reference ancestor walk or scope copying.
    std::size_t next = 0;
    for (const auto binding : bindings) {
      Step();
      if (next && Compare(bindings[next - 1].name, binding.name) == 0 && binding.begin <= bindings[next - 1].end)
        bindings[next - 1].end = std::max(bindings[next - 1].end, binding.end);
      else bindings[next++] = binding;
    }
    bindings.resize(next);
  }
  bool Shadows(const std::vector<BindingInterval>& bindings, std::u16string_view name, std::size_t point) {
    const auto found = std::upper_bound(bindings.begin(), bindings.end(), point,
        [&](std::size_t at, const BindingInterval& binding) {
          const auto order = Compare(name, binding.name);
          return order < 0 || (order == 0 && at < binding.begin);
        });
    if (found == bindings.begin()) return false;
    const auto& before = *(found - 1);
    return Compare(name, before.name) == 0 && point < before.end;
  }
  void CheckModuleNames() {
    std::sort(modules_.begin(), modules_.end(), [&](const auto& a, const auto& b) {
      Step(); return a.target < b.target;
    });
    for (std::size_t i = 1; i < modules_.size(); ++i) {
      Step(); const auto& a = modules_[i - 1]; const auto& b = modules_[i];
      member_ = b.member; name_ = b.id;
      Require(a.target != b.target || Compare(a.name, b.name) == 0, Error::kModuleBinding);
    }
    std::sort(modules_.begin(), modules_.end(), [&](const auto& a, const auto& b) {
      return Compare(a.name, b.name) < 0;
    });
    for (std::size_t i = 1; i < modules_.size(); ++i) {
      Step(); const auto& a = modules_[i - 1]; const auto& b = modules_[i];
      member_ = b.member; name_ = b.id;
      Require(Compare(a.name, b.name) != 0 || a.target == b.target, Error::kModuleBinding);
    }
    name_.reset();
  }
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
  void Destination(std::u16string_view name, BindingTarget target = BindingTarget::kGlobal) {
    Require(scope_ != nullptr, Error::kMissingContext);
    // VerifyTreeStructure already enforced the shared global-name policy.
    // Borrow record-owned sorted indexes, not a copied union per descendant.
    // Family-local lexical ranges were checked separately at creation scopes.
    Require(!Contains(scope_->enclosing_bindings, name) && !Contains(scope_->record_bindings, name) && !Contains(scope_->import_names, name) &&
        !Contains(scope_->parameter_bindings, name) && !Equal(name, scope_->class_binding) &&
        !Equal(name, scope_->self_binding) &&
        (target == BindingTarget::kRealm || !scope_->realm_binding || !Equal(name, scope_->realm_binding->name)) &&
        (target == BindingTarget::kSymbolConstructor || !scope_->symbol_binding || !Equal(name, scope_->symbol_binding->name)) &&
        (target == BindingTarget::kArgumentSnapshot || !scope_->argument_snapshot_binding ||
            !Equal(name, scope_->argument_snapshot_binding->name)),
        Error::kShadowedGlobal);
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
  void Step(std::uint64_t count = 1) { if(count>left_.work)PrintCharge(TreePrintBudgetKind::kWork,count,left_.work,initial_.work); Require(count <= left_.work, Error::kWorkLimit); left_.work -= count; }
  void Admit(std::size_t count, std::size_t width) {
    if(count>left_.bytes/width)PrintByteCharge(count,width,left_.bytes,initial_.bytes,initial_.bytes-left_.bytes); Require(count <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= count * width; peak_=std::max(peak_,initial_.bytes-left_.bytes);
  }
  template<class T> void Array(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Step(count); values.resize(count); Admit(values.capacity() - count, sizeof(T));
  }
  [[noreturn]] void Fail(Error error) const { throw FamilyEnclosureFailure{error, member_, name_}; }
  void Require(bool ok, Error error) const { if (!ok) Fail(error); }
  const ConstructedFamily& family_;
  analysis::SemanticBudget initial_, left_;
  std::size_t peak_=0,input_bytes_=0;
  EnclosureExtent extent_;
  const GlobalScope* scope_;
  ModuleRole root_role_;
  FamilyId member_{0};
  std::optional<NameId> name_;
  std::vector<TreeStructure> structures_;
  std::vector<std::size_t> offsets_;
  std::vector<std::optional<std::size_t>> parents_;
  std::vector<ScopeInterval> intervals_;
  std::vector<BindingInterval> bindings_;
  std::vector<BindingInterval> locals_;
  std::vector<ModuleName> modules_;
};
}  // namespace
std::variant<FamilyEnclosure, FamilyEnclosureFailure> VerifyFamilyEnclosure(
    const ConstructedFamily& family, analysis::SemanticBudget budget) {
  try { return EnclosureCheck(family, budget).Run();
  } catch (const FamilyEnclosureFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return FamilyEnclosureFailure{Error::kResourceLimit, {0}, {}};
  } catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return FamilyEnclosureFailure{Error::kResourceLimit, {0}, {}}; }
}
std::variant<FamilyEnclosure, FamilyEnclosureFailure> VerifyFamilyDestination(
    const ConstructedFamily& family, const GlobalScope* scope, analysis::SemanticBudget budget, ModuleRole root_role) {
  try { return EnclosureCheck(family, budget, EnclosureExtent::kDestination, scope, root_role).Run();
  } catch (const FamilyEnclosureFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return FamilyEnclosureFailure{Error::kResourceLimit, {0}, {}};
  } catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return FamilyEnclosureFailure{Error::kResourceLimit, {0}, {}}; }
}
}  // namespace abcd::source
