#include "abcd/source/module_enclosure.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "abcd/source/global_reference.h"

namespace abcd::source {
namespace {
using Error = ModuleEnclosureError;
void Require(bool condition, Error error) { if (!condition) throw error; }
bool Matches(const RouteInputIdentity& identity, const abc::Header& header) {
  return identity.file_size == header.file_size && identity.profile == header.profile &&
      identity.abc_version == header.version;
}
class Budget {
 public:
  explicit Budget(analysis::SemanticBudget budget) : remaining(budget) {}
  void Step(std::uint64_t count = 1) {
    Require(count <= remaining.work, Error::kWorkLimit); remaining.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= remaining.bytes / width, Error::kResourceLimit); remaining.bytes -= count * width;
  }
  template <class T> void Reserve(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); values.reserve(count); Admit(values.capacity() - count, sizeof(T));
  }
  template <class A, class B> bool Less(A a, B b) {
    Step(std::min(a.size(), b.size()) + 1);
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](auto x, auto y) {
      return static_cast<std::make_unsigned_t<decltype(x)>>(x) <
          static_cast<std::make_unsigned_t<decltype(y)>>(y);
    });
  }
  template <class A, class B> bool Equal(A a, B b) { return !Less(a, b) && !Less(b, a); }
  bool Contains(std::span<const std::string_view> names, std::u16string_view name) {
    const auto found = std::lower_bound(names.begin(), names.end(), name,
        [&](auto a, auto b) { return Less(a, b); });
    return found != names.end() && Equal(*found, name);
  }
  analysis::SemanticBudget remaining;
};
}  // namespace

std::variant<ModuleEnclosure, ModuleEnclosureError> ModuleEnclosure::Build(
    const corpus::AbcFile& file, const abc::Header& header,
    const RecordModuleContext& context, RouteInputIdentity identity,
    ModuleOwnerId owner, analysis::SemanticBudget allowance) {
  try {
    Budget budget(allowance); budget.Step(); budget.Admit(1, sizeof(ModuleEnclosure));
    Require(Matches(identity, header) && file.Size() == identity.file_size, Error::kIdentity);
    const auto module = context.ForOwner(file, context.record());
    Require(module != nullptr, Error::kMissingModule);
    Require(!file.CheckUnchanged(), Error::kFileChanged);
    ModuleEnclosure result(context, identity, owner);
    const auto count = module->imports.size() + module->namespace_imports.size();
    budget.Reserve(result.names_, count); budget.Reserve(result.sorted_, count);
    const auto add = [&](abc::EntityOffset offset) {
      budget.Step();
      auto decoded = abc::ReadStringEntity(file, header, offset, {budget.remaining.bytes});
      const auto* name = std::get_if<std::u16string>(&decoded);
      if (!name) {
        const auto error = std::get<abc::MetadataError>(decoded);
        if (error == abc::MetadataError::kResourceLimit) throw Error::kResourceLimit;
        if (error == abc::MetadataError::kFileChanged) throw Error::kFileChanged;
        throw Error::kMetadata;
      }
      // Charge decoded scratch while the narrowed owned ASCII string coexists.
      const auto scratch = (name->capacity() + 1) * sizeof(char16_t);
      budget.Admit(scratch, 1); budget.Step(name->size());
      Require(AllowsGlobalReference(*name, {}), Error::kName);
      budget.Admit(name->size() + 1, 1);
      result.names_.emplace_back(name->begin(), name->end());
      budget.Admit(result.names_.back().capacity() - name->size(), 1);
      budget.remaining.bytes += scratch;
    };
    for (const auto& entry : module->imports) add(entry.local);
    for (const auto& entry : module->namespace_imports) add(entry.local);
    for (const auto& name : result.names_) { budget.Step(); result.sorted_.push_back(name); }
    std::sort(result.sorted_.begin(), result.sorted_.end(), [&](auto a, auto b) { return budget.Less(a, b); });
    for (std::size_t i = 1; i < result.sorted_.size(); ++i)
      Require(!budget.Equal(result.sorted_[i - 1], result.sorted_[i]), Error::kDuplicate);
    Require(!file.CheckUnchanged(), Error::kFileChanged);
    result.allocated_bytes_ = allowance.bytes - budget.remaining.bytes;
    result.work_ = allowance.work - budget.remaining.work;
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

ModuleImportResult ModuleEnclosure::WriteImports(const corpus::AbcFile& file,
    const abc::Header& header, std::ostream& output, EmitBudget budget, abc::StringBudget strings) const {
  const auto module = context_.ForOwner(file, context_.record());
  if (!module || !Matches(identity_, header) || file.CheckUnchanged())
    return ModuleImportFailure{ModuleImportError::kString, context_.record(), {}};
  return WriteModuleImports(file, header, *module, output, budget, strings, names_);
}

std::variant<ModuleDestination, ModuleEnclosureError> VerifyModuleDestination(
    const FunctionTree& tree, abc::EntityOffset record, const GlobalScope* scope,
    analysis::SemanticBudget allowance) {
  try {
    Budget budget(allowance); budget.Step();
    if (tree.modules.empty()) {
      Require(!tree.module_scope, Error::kBinding);
      return ModuleDestination{allowance.work - budget.remaining.work};
    }
    Require(scope && scope->module_enclosure, Error::kMissingModule);
    const auto& enclosure = *scope->module_enclosure;
    Require(tree.identity == enclosure.Identity() && record == enclosure.Context().record(), Error::kIdentity);
    Require(tree.module_scope && tree.module_scope->value < tree.scopes.size() &&
        tree.scopes[tree.module_scope->value].kind == ScopeKind::kModule, Error::kBinding);
    for (const auto& alias : tree.modules) {
      budget.Step();
      Require(alias.target.owner == enclosure.Owner() && alias.target.symbol.value < enclosure.Names().size() + scope->module_local_names.size() &&
          alias.symbol.value < tree.symbols.size() && alias.access == ModuleAccess::kReadOnly, Error::kBinding);
      const auto& symbol = tree.symbols[alias.symbol.value];
      Require(symbol.scope == *tree.module_scope && symbol.kind == BindingKind::kModule &&
          symbol.declaration == DeclarationKind::kConst && symbol.evidence == NameEvidence::kMetadata &&
          symbol.name.value < tree.names.size(), Error::kBinding);
      const std::u16string_view name = tree.names[symbol.name.value];
      const auto slot = alias.target.symbol.value;
      const bool local = slot >= enclosure.Names().size();
      if (local) Require(budget.Equal(name, std::u16string_view(
          scope->module_local_names[slot - enclosure.Names().size()])), Error::kName);
      else Require(budget.Equal(name, std::string_view(enclosure.Names()[slot])), Error::kName);
      Require(local || !budget.Contains(scope->record_bindings, name), Error::kShadowed);
      Require(!budget.Contains(scope->enclosing_bindings, name) &&
          !budget.Contains(scope->parameter_bindings, name) &&
          !budget.Equal(name, scope->class_binding) && !budget.Equal(name, scope->self_binding) &&
          (!scope->realm_binding || !budget.Equal(name, scope->realm_binding->name)) &&
          (!scope->symbol_binding || !budget.Equal(name, scope->symbol_binding->name)) &&
          (!scope->argument_snapshot_binding || !budget.Equal(name, scope->argument_snapshot_binding->name)), Error::kShadowed);
    }
    return ModuleDestination{allowance.work - budget.remaining.work};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
