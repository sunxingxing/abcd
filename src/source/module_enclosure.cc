#include "tree_print_trace.h"
#include "abcd/source/module_enclosure.h"
#include "shared_module.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "abcd/source/global_reference.h"
#include "abcd/source/scalar_expression.h"

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
  explicit Budget(analysis::SemanticBudget budget) : remaining(budget), initial(budget) {}
  void Step(std::uint64_t count = 1) {
    if(count>remaining.work)PrintCharge(TreePrintBudgetKind::kWork,count,remaining.work,initial.work); Require(count <= remaining.work, Error::kWorkLimit); remaining.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    if(count>remaining.bytes/width)PrintByteCharge(count,width,remaining.bytes,initial.bytes,initial.bytes-remaining.bytes); Require(count <= remaining.bytes / width, Error::kResourceLimit); remaining.bytes -= count * width;
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
  const analysis::SemanticBudget initial;
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
    if(const auto* flags=context.Attributes(file,context.record());flags&&flags->shared.value_or(false)) {
      budget.Step(module->request_phases.size()+1);
      const auto proof=SharedModuleProof::Build(context,file,context.record(),identity.contract);
      Require(std::holds_alternative<SharedModuleProof>(proof),Error::kModuleKind);
      result.shared_=true;
    }
    const auto count = module->imports.size() + module->namespace_imports.size();
    budget.Reserve(result.names_, count); budget.Reserve(result.sorted_, count);
    budget.Reserve(result.loadings_, count);
    Require(module->request_phases.empty() || module->request_phases.size()==module->requests.size(), Error::kRequestPhases);
    const auto loading = [&](abc::ModuleRequestId request) {
      budget.Step();Require(request.value<module->requests.size(),Error::kBinding);
      const bool lazy=!module->request_phases.empty()&&module->request_phases[request.value]==abc::ModuleRequestPhase::kLazy;
      Require(!lazy||IsSourceContract(identity.contract),Error::kContract);
      Require(!lazy||context.OrdinarySynchronous(file,context.record()),Error::kModuleKind);
      result.has_lazy_ |= lazy;
      return lazy?analysis::ModuleLoading::kLazy:analysis::ModuleLoading::kOrdinary;
    };
    for(const auto& entry:module->imports)result.loadings_.push_back(loading(entry.request));
    for(const auto& entry:module->namespace_imports) {
      const auto kind=loading(entry.request);Require(kind==analysis::ModuleLoading::kOrdinary,Error::kModuleKind);
      result.loadings_.push_back(kind);
    }
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
      Require(IsSourceIdentifier(std::u16string_view{*name}), Error::kName);
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
  } catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return Error::kResourceLimit;
  } catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return Error::kResourceLimit; }
}

ModuleImportResult ModuleEnclosure::WriteImports(const corpus::AbcFile& file,
    const abc::Header& header, std::ostream& output, EmitBudget budget, abc::StringBudget strings) const {
  const auto module = context_.ForOwner(file, context_.record());
  if (!module || !Matches(identity_, header) || file.CheckUnchanged())
    return ModuleImportFailure{ModuleImportError::kString, context_.record(), {}};
  std::size_t prefix=0;
  if(shared_) {
    const auto proof=SharedModuleProof::Build(context_,file,context_.record(),identity_.contract);
    if(!std::holds_alternative<SharedModuleProof>(proof))return ModuleImportFailure{ModuleImportError::kModuleKind,context_.record(),{}};
    constexpr std::string_view directive="\"use shared\";\n";
    if(directive.size()>budget.bytes)return ModuleImportFailure{ModuleImportError::kOutputLimit,context_.record(),{}};
    output.write(directive.data(),directive.size());
    if(!output)return ModuleImportFailure{ModuleImportError::kWrite,context_.record(),{}};
    prefix=directive.size();budget.bytes-=prefix;
  }
  auto result=WriteModuleImports(file, header, *module, output, budget, strings, names_, identity_.contract);
  if(auto* bytes=std::get_if<std::size_t>(&result))*bytes+=prefix;
  return result;
}

std::variant<ModuleDestination, ModuleEnclosureError> VerifyModuleDestination(
    const FunctionTree& tree, abc::EntityOffset record, const GlobalScope* scope,
    analysis::SemanticBudget allowance, ModuleRole role) {
  try {
    Budget budget(allowance); budget.Step();
    if (tree.modules.empty()) {
      Require(!tree.module_scope, Error::kBinding);
      for(const auto& node:tree.expressions) {
        budget.Step();if(const auto* read=std::get_if<SymbolExpr>(&node.expression))
          Require(read->loading==analysis::ModuleLoading::kOrdinary && read->domain==analysis::ModuleDomain::kOrdinary,Error::kBinding);
      }
      return ModuleDestination{allowance.work - budget.remaining.work};
    }
    Require(scope && scope->module_enclosure, Error::kMissingModule);
    const auto& enclosure = *scope->module_enclosure;
    Require(tree.identity == enclosure.Identity() && record == enclosure.Context().record(), Error::kIdentity);
    Require(tree.module_scope && tree.module_scope->value < tree.scopes.size() &&
        tree.scopes[tree.module_scope->value].kind == ScopeKind::kModule, Error::kBinding);
    Require(scope->module_local_evidence.empty() ||
        scope->module_local_evidence.size() == scope->module_local_names.size(), Error::kBinding);
    std::vector<analysis::ModuleLoading> loadings;
    if(enclosure.HasLazy()) {budget.Reserve(loadings,tree.symbols.size());loadings.resize(tree.symbols.size(),analysis::ModuleLoading::kOrdinary);}
    for (const auto& alias : tree.modules) {
      budget.Step();
      Require(alias.target.owner == enclosure.Owner() && alias.target.symbol.value < enclosure.Names().size() + scope->module_local_names.size() &&
          alias.symbol.value < tree.symbols.size(), Error::kBinding);
      const auto& symbol = tree.symbols[alias.symbol.value];
      const auto slot = alias.target.symbol.value;
      const bool local = slot >= enclosure.Names().size();
      // A Sendable tree's local module reads are LDSENDABLELOCALMODULEVAR of
      // the same module record (FunctionInput requires owner/load domain
      // agreement), which es2panda re-emits for the same local reference.
      if(!local && enclosure.HasLazy()) {Require(slot<enclosure.Loadings().size(),Error::kBinding);loadings[alias.symbol.value]=enclosure.Loadings()[slot];}
      const auto evidence = local && !scope->module_local_evidence.empty()
          ? scope->module_local_evidence[slot - enclosure.Names().size()] : NameEvidence::kMetadata;
      Require(symbol.evidence == evidence && symbol.name.value < tree.names.size(), Error::kBinding);
      if (alias.local) {
        Require(role == ModuleRole::kInitializer && alias.access == ModuleAccess::kMutable &&
            alias.target.symbol.value >= enclosure.Names().size() && symbol.scope == tree.body_scope &&
            symbol.kind == BindingKind::kLocal && symbol.declaration == DeclarationKind::kLet, Error::kBinding);
      } else {
        Require((alias.access == ModuleAccess::kReadOnly || (local && alias.access == ModuleAccess::kMutable)) &&
            symbol.scope == *tree.module_scope && symbol.kind == BindingKind::kModule &&
            symbol.declaration == (alias.access == ModuleAccess::kMutable ? DeclarationKind::kLet : DeclarationKind::kConst), Error::kBinding);
      }
      const std::u16string_view name = tree.names[symbol.name.value];
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
    for(const auto& node:tree.expressions) {
      budget.Step();if(const auto* read=std::get_if<SymbolExpr>(&node.expression)) {
        Require(read->symbol.value<tree.symbols.size(),Error::kBinding);
        const bool module=tree.symbols[read->symbol.value].kind==BindingKind::kModule;
        Require(read->domain==(module?tree.module_domain:analysis::ModuleDomain::kOrdinary),Error::kBinding);
        Require(enclosure.HasLazy() ? read->symbol.value<loadings.size()&&read->loading==loadings[read->symbol.value] : read->loading==analysis::ModuleLoading::kOrdinary,Error::kBinding);
      }
    }
    return ModuleDestination{allowance.work - budget.remaining.work};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { PrintCharge(TreePrintBudgetKind::kHostAllocation,{},0,0); return Error::kResourceLimit;
  } catch (const std::length_error&) { PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},0,0); return Error::kResourceLimit; }
}
}  // namespace abcd::source
