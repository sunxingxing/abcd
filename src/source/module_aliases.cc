#include "module_aliases.h"
#include "module_binding_name.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <functional>
#include <type_traits>

#include "abcd/abc/string_entity.h"
#include "abcd/corpus/abc_file.h"

namespace abcd::source {
namespace {
using Error = ModuleAliasError;
struct AliasSource {
  const RouteInputIdentity& identity;
  abc::EntityOffset method, record;
  const analysis::ValueProgram& graph;
  const ModuleGuards& guards;
  std::variant<std::reference_wrapper<const FunctionInput>,
      std::reference_wrapper<const analysis::PhasedValueInput>> input;
};
class AliasBinder {
 public:
  AliasBinder(const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, AliasSource input, const RecordModuleContext& context,
      ModuleOwnerId owner, NameId first_name, analysis::SemanticBudget budget)
      : file_(file), header_(header), index_(index), input_(input),
        context_(context), budget_(budget), bytes_(budget.bytes), work_(budget.work),
        result_{input.identity, input.method, input.record, owner, input.graph.revision,
            input.graph.cfg_revision, {}, {}, first_name} {}

  ModuleAliases Bind() {
    const auto& graph = input_.graph;
    const auto& identity = input_.identity;
    Require(context_.record() == input_.record && identity.profile == header_.profile &&
        identity.abc_version == header_.version && identity.file_size == header_.file_size,
        Error::kIdentity);
    const auto guards = std::visit([&](const auto& source) {
      if constexpr (std::is_same_v<std::decay_t<decltype(source.get())>, FunctionInput>)
        return VerifyModuleGuards(source.get(), graph, input_.guards, {bytes_, work_});
      else return VerifyPhasedModuleGuards(source.get(), graph, input_.guards, {bytes_, work_});
    }, input_.input);
    if (const auto* error = std::get_if<ModuleGuardError>(&guards)) throw *error;
    Step(std::get<std::uint64_t>(guards));
    const auto module = context_.ForOwner(file_, input_.record);
    auto resolved = ResolveModuleReads(file_, header_, module.get(), graph, {bytes_, work_});
    if (const auto* error = std::get_if<ModuleReadFailure>(&resolved)) throw *error;
    const auto& reads = std::get<ModuleReads>(resolved);
    Admit(reads.allocated_bytes, 1); Step(reads.work);
    Admit(1, sizeof(AliasBinder));
    Require(reads.bindings.size() == input_.guards.pairs.size(), Error::kCoverage);
    const auto count = reads.bindings.size();
    Reserve(result_.evaluations, count);
    Reserve(guard_names_, count);
    Reserve(local_names_, count);
    Reserve(order_, count);
    std::size_t external = 0;
    if (module) {
      Require(module->imports.size() <= kMaxId && module->namespace_imports.size() <=
          kMaxId - module->imports.size(), Error::kResourceLimit);
      external = module->imports.size() + module->namespace_imports.size();
    }
    for (std::size_t i = 0; i < count; ++i) {
      Step();
      const auto& read = reads.bindings[i];
      const auto pair = input_.guards.pairs[i];
      Require(read.operation == pair.read, Error::kCoverage);
      const auto local = std::visit([](const auto& binding) { return binding.local; }, read.target);
      local_names_.push_back(local);
      if (pair.guard)
        guard_names_.push_back(*std::get<analysis::ValueGuard>(graph.ops[pair.guard->value].operation).name);
      else guard_names_.push_back(std::monostate{});
      order_.push_back(i);
      // Allocate source identity by the record inventory, never by function
      // encounter order or name equality. Equal names in different VM slots
      // remain distinct bindings; source scope verification may refuse them.
      const auto base = read.read.space == analysis::ModuleSpace::kLocal ? external : 0;
      Require(read.read.slot.value <= kMaxId - base, Error::kResourceLimit);
      result_.evaluations.push_back({pair, read.read,
          {result_.owner, {static_cast<std::uint32_t>(base + read.read.slot.value)}}});
    }
    ResolveGuardNames();
    DecodeNames();
    for (std::size_t i = 0; i < count; ++i) {
      Step();
      if (!result_.evaluations[i].pair.guard) continue;
      if (classes_[NameIndex(local_names_[i])] != classes_[NameIndex(std::get<abc::EntityOffset>(guard_names_[i]))])
        throw ModuleAliasFailure{Error::kGuardName,
            graph.origins[graph.ops[result_.evaluations[i].pair.guard->value].origin.value].pc};
    }
    std::optional<ModuleBindingNames> local_bindings;
    if (std::any_of(reads.bindings.begin(), reads.bindings.end(), [](const auto& read) {
        return read.read.space == analysis::ModuleSpace::kLocal;
      })) {
      auto named = ResolveModuleBindingNames(file_, header_, *module, {bytes_, work_});
      if (const auto* failure = std::get_if<ModuleBindingNameFailure>(&named)) {
        std::visit([&](auto error) {
          if constexpr (std::is_same_v<decltype(error), abc::MetadataError>) throw error;
          else switch (error) {
            case ModuleBindingNameError::kResourceLimit: throw Error::kResourceLimit;
            case ModuleBindingNameError::kWorkLimit: throw Error::kWorkLimit;
            case ModuleBindingNameError::kName: throw Error::kCoverage;
          }
        }, *failure);
      }
      local_bindings.emplace(std::get<ModuleBindingNames>(std::move(named)));
      Admit(local_bindings->allocated_bytes, 1); Step(local_bindings->work);
    }
    std::sort(order_.begin(), order_.end(), [&](auto a, auto b) {
      Step(); return result_.evaluations[a].target < result_.evaluations[b].target;
    });
    std::size_t aliases = 0;
    std::optional<ModuleSymbol> previous;
    for (const auto i : order_) {
      Step();
      if (previous != result_.evaluations[i].target) ++aliases;
      previous = result_.evaluations[i].target;
    }
    Require(aliases <= kMaxId - result_.first_name.value, Error::kResourceLimit);
    Reserve(result_.aliases, aliases); Reserve(result_.names, aliases);
    previous.reset();
    for (const auto i : order_) {
      Step();
      const auto& evaluation = result_.evaluations[i];
      if (previous == evaluation.target) continue;
      previous = evaluation.target;
      const NameId name{result_.first_name.value + static_cast<std::uint32_t>(result_.names.size())};
      const bool local = evaluation.read.space == analysis::ModuleSpace::kLocal;
      const auto slot = evaluation.read.slot.value;
      Require(!local || (local_bindings && slot < local_bindings->locals.size()), Error::kCoverage);
      const auto& text = local ? local_bindings->locals[slot] : decoded_[NameIndex(local_names_[i])];
      Step(text.size()); Admit(text.size(), sizeof(char16_t)); Admit(1, sizeof(char16_t));
      auto& copy = result_.names.emplace_back(text);
      Admit(copy.capacity() - text.size(), sizeof(char16_t));
      // This adapter proves reads only. A local-export row does NOT prove a
      // let/const declaration or permission to write. ReadOnly restricts this
      // alias, not the live binding's value; record/store proof may widen it.
      result_.aliases.push_back({evaluation.target, name, ModuleAccess::kReadOnly,
          local ? local_bindings->evidence[slot] : NameEvidence::kMetadata});
    }
    if (file_.CheckUnchanged()) throw abc::MetadataError::kFileChanged;
    result_.allocated_bytes = budget_.bytes - bytes_;
    result_.work = budget_.work - work_;
    return std::move(result_);
  }
 private:
  static constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
  void Require(bool condition, Error error) const { if (!condition) throw error; }
  void Step(std::uint64_t count = 1) {
    Require(count <= work_, Error::kWorkLimit); work_ -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= bytes_ / width, Error::kResourceLimit); bytes_ -= count * width;
  }
  template <class T> void Reserve(std::vector<T>& array, std::size_t count) {
    Admit(count, sizeof(T)); array.reserve(count); Admit(array.capacity() - count, sizeof(T));
  }
  void ResolveGuardNames() {
    std::sort(order_.begin(), order_.end(), [&](auto a, auto b) {
      Step();
      const auto* left = std::get_if<abc::PoolIndex>(&guard_names_[a]);
      const auto* right = std::get_if<abc::PoolIndex>(&guard_names_[b]);
      return left && right ? *left < *right : !left && right;
    });
    for (std::size_t first = 0; first < order_.size();) {
      Step();
      if (std::holds_alternative<std::monostate>(guard_names_[order_[first]])) { ++first; continue; }
      const auto slot = std::get<abc::PoolIndex>(guard_names_[order_[first]]);
      auto end = first + 1;
      while (end < order_.size()) {
        const auto* next = std::get_if<abc::PoolIndex>(&guard_names_[order_[end]]);
        if (!next || *next != slot) break;
        Step(); ++end;
      }
      Step(std::bit_width(index_.regions.size()) + 1);
      const auto resolved = abc::ResolvePoolReference(file_, header_, index_, input_.method, slot,
          abc::PoolEntityKind::kString);
      if (const auto* error = std::get_if<abc::MetadataError>(&resolved)) throw *error;
      const auto entity = std::get<abc::PoolReference>(resolved).offset;
      for (auto i = first; i < end; ++i) { Step(); guard_names_[order_[i]] = entity; }
      first = end;
    }
  }
  std::size_t NameIndex(abc::EntityOffset entity) {
    const auto found = std::lower_bound(entities_.begin(), entities_.end(), entity,
        [&](auto a, auto b) { Step(); return a < b; });
    Require(found != entities_.end() && *found == entity, Error::kCoverage);
    return static_cast<std::size_t>(found - entities_.begin());
  }
  void DecodeNames() {
    constexpr std::size_t kNamesPerRead = 2; // Metadata local and guard diagnostic.
    Require(local_names_.size() <= kMaxId / kNamesPerRead, Error::kResourceLimit);
    Reserve(entities_, local_names_.size() * kNamesPerRead);
    for (std::size_t i = 0; i < local_names_.size(); ++i) {
      Step(); entities_.push_back(local_names_[i]);
      if (const auto* guard = std::get_if<abc::EntityOffset>(&guard_names_[i])) entities_.push_back(*guard);
    }
    std::sort(entities_.begin(), entities_.end(), [&](auto a, auto b) { Step(); return a < b; });
    entities_.erase(std::unique(entities_.begin(), entities_.end(), [&](auto a, auto b) {
      Step(); return a == b;
    }), entities_.end());
    Reserve(decoded_, entities_.size()); Reserve(classes_, entities_.size());
    std::vector<std::size_t> order;
    Reserve(order, entities_.size());
    for (const auto entity : entities_) {
      Step();
      auto parsed = abc::ReadStringEntity(file_, header_, entity, {bytes_});
      if (const auto* error = std::get_if<abc::MetadataError>(&parsed)) throw *error;
      auto text = std::get<std::u16string>(std::move(parsed));
      Step(text.size()); Admit(text.capacity(), sizeof(char16_t)); Admit(1, sizeof(char16_t));
      order.push_back(decoded_.size()); classes_.push_back(0);
      decoded_.push_back(std::move(text));
    }
    // Intern by text once. Repeated long names at different entities must not
    // incur a full string comparison for every instruction occurrence.
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
      Step();
      return std::lexicographical_compare(decoded_[a].begin(), decoded_[a].end(),
          decoded_[b].begin(), decoded_[b].end(), [&](char16_t x, char16_t y) { Step(); return x < y; });
    });
    std::size_t group = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
      Step();
      if (i) {
        const auto& a = decoded_[order[i - 1]];
        const auto& b = decoded_[order[i]];
        Step(std::min(a.size(), b.size()) + 1);
        if (a != b) ++group;
      }
      classes_[order[i]] = group;
    }
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::MetadataIndex& index_;
  AliasSource input_;
  const RecordModuleContext& context_;
  analysis::SemanticBudget budget_;
  std::size_t bytes_;
  std::uint64_t work_;
  ModuleAliases result_;
  std::vector<std::variant<std::monostate, abc::PoolIndex, abc::EntityOffset>> guard_names_;
  std::vector<abc::EntityOffset> local_names_, entities_;
  std::vector<std::size_t> order_, classes_;
  std::vector<std::u16string> decoded_;
};
std::variant<ModuleAliases, ModuleAliasFailure> BindAliases(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, AliasSource input, const RecordModuleContext& context,
    ModuleOwnerId owner, NameId first_name, analysis::SemanticBudget budget) {
  try { return AliasBinder(file, header, index, input, context, owner, first_name, budget).Bind();
  } catch (const ModuleAliasFailure& error) { return error;
  } catch (Error error) { return ModuleAliasFailure{error};
  } catch (ModuleGuardError error) { return ModuleAliasFailure{error};
  } catch (const ModuleReadFailure& error) { return ModuleAliasFailure{error, error.pc};
  } catch (abc::MetadataError error) { return ModuleAliasFailure{error};
  } catch (const std::bad_alloc&) { return ModuleAliasFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return ModuleAliasFailure{Error::kResourceLimit}; }
}
}  // namespace
std::variant<ModuleAliases, ModuleAliasFailure> BindModuleAliases(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const FunctionInput& input,
    const FunctionValues& values, const RecordModuleContext& context,
    ModuleOwnerId owner, NameId first_name, analysis::SemanticBudget budget) {
  if (values.identity != input.Identity() || values.method != input.Method() ||
      values.record != input.Record() || values.capture_mode != input.Mode())
    return ModuleAliasFailure{Error::kIdentity};
  return BindAliases(file, header, index, {input.Identity(), input.Method(), input.Record(),
      values.graph, values.modules, std::cref(input)}, context, owner, first_name, budget);
}
std::variant<ModuleAliases, ModuleAliasFailure> BindPhasedModuleAliases(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const PhasedModuleInput& input,
    const RecordModuleContext& context, ModuleOwnerId owner, NameId first_name,
    analysis::SemanticBudget budget) {
  return BindAliases(file, header, index, {input.identity, input.method, input.record,
      input.graph, input.guards, std::cref(input.phases)}, context, owner, first_name, budget);
}
}  // namespace abcd::source
