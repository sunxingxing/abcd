#include "module_reads.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/corpus/abc_file.h"
#include "abcd/abc/string_entity.h"

namespace abcd::source {
namespace {
using Error = ModuleReadError;
class ReadResolver {
 public:
  ReadResolver(const corpus::AbcFile& file, const abc::Header& header,
      const abc::ModuleInfo* module, const analysis::ValueProgram& graph,
      analysis::SemanticBudget budget, SourceContract contract)
      : file_(file), header_(header), module_(module), graph_(graph),
        budget_(budget), bytes_(budget.bytes), work_(budget.work), contract_(contract) {}
  ModuleReads Resolve() {
    if (file_.Size() != header_.file_size || file_.CheckUnchanged())
      throw abc::MetadataError::kFileChanged;
    if (graph_.ops.size() > std::numeric_limits<std::uint32_t>::max())
      throw Error::kResourceLimit;
    Admit(1, sizeof(ReadResolver));
    std::size_t count = 0;
    bool local = false;
    for (const auto& op : graph_.ops) {
      Step();
      if (op.life != analysis::ValueLife::kLive) continue;
      if (const auto* read = std::get_if<analysis::ValueModuleRead>(&op.operation)) {
        ++count;
        local |= read->space == analysis::ModuleSpace::kLocal;
      }
    }
    if (count && !module_) throw Error::kMissingModule;
    Reserve(result_.bindings, count);
    if (local) Locals();
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step();
      const auto& op = graph_.ops[i];
      if (op.life != analysis::ValueLife::kLive) continue;
      const auto* read = std::get_if<analysis::ValueModuleRead>(&op.operation);
      if (!read) continue;
      try {
        auto canonical = *read;
        auto target = Target(canonical);
        result_.bindings.push_back({{static_cast<std::uint32_t>(i)}, *read, std::move(target), canonical.slot});
      } catch (Error error) {
        const auto pc = op.origin.value < graph_.origins.size()
            ? std::optional{graph_.origins[op.origin.value].pc} : std::nullopt;
        throw ModuleReadFailure{error, pc};
      }
    }
    if (file_.CheckUnchanged()) throw abc::MetadataError::kFileChanged;
    result_.revision = graph_.revision;
    result_.cfg_revision = graph_.cfg_revision;
    // Includes transient local-name indexes, conservatively admitted by callers.
    result_.allocated_bytes = budget_.bytes - bytes_;
    result_.work = budget_.work - work_;
    return std::move(result_);
  }
 private:
  void Step(std::uint64_t count = 1) {
    if (count > work_) throw Error::kWorkLimit;
    work_ -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    if (count > bytes_ / width) throw Error::kResourceLimit;
    bytes_ -= count * width;
  }
  template <class T> void Reserve(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); values.reserve(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  void Locals() {
    // v7 ModuleDataAccessor::EnumerateLocalExportEntry compares decoded string
    // contents, not entity IDs. Decode each distinct entity once, then walk
    // original table order; sorting names would incorrectly merge distant slots.
    std::vector<abc::EntityOffset> entities;
    Reserve(entities, module_->local_exports.size());
    for (const auto& entry : module_->local_exports) { Step(); entities.push_back(entry.local); }
    std::sort(entities.begin(), entities.end(), [&](auto a, auto b) { Step(); return a < b; });
    const auto end = std::unique(entities.begin(), entities.end(), [&](auto a, auto b) { Step(); return a == b; });
    entities.erase(end, entities.end());
    std::vector<std::u16string> names;
    Reserve(names, entities.size());
    for (const auto entity : entities) {
      Step();
      auto parsed = abc::ReadStringEntity(file_, header_, entity, {bytes_});
      if (const auto* error = std::get_if<abc::MetadataError>(&parsed)) throw *error;
      auto name = std::get<std::u16string>(std::move(parsed));
      Step(name.size()); Admit(name.capacity(), sizeof(char16_t)); Admit(1, sizeof(char16_t));
      names.push_back(std::move(name));
    }
    Reserve(locals_, module_->local_exports.size());
    std::optional<std::size_t> previous;
    for (const auto& entry : module_->local_exports) {
      Step();
      const auto found = std::lower_bound(entities.begin(), entities.end(), entry.local,
          [&](auto a, auto b) { Step(); return a < b; });
      const auto name = static_cast<std::size_t>(found - entities.begin());
      bool equal = previous && *previous == name;
      if (previous && !equal) {
        Step(std::min(names[*previous].size(), names[name].size()) + 1);
        equal = names[*previous] == names[name];
      }
      if (!equal) locals_.push_back(entry.local);
      previous = name;
    }
  }
  abc::EntityOffset Request(abc::ModuleRequestId request) const {
    if (request.value >= module_->requests.size()) throw Error::kRequest;
    return module_->requests[request.value];
  }
  void Loading(analysis::ValueModuleRead read, abc::ModuleRequestId request) const {
    if (request.value >= module_->requests.size()) throw Error::kRequest;
    if (!module_->request_phases.empty() && module_->request_phases.size() != module_->requests.size())
      throw Error::kRequestPhases;
    const bool lazy = !module_->request_phases.empty() &&
        module_->request_phases[request.value] == abc::ModuleRequestPhase::kLazy;
    if (lazy != (read.loading == analysis::ModuleLoading::kLazy)) throw Error::kBinding;
    if (!IsSourceContract(contract_)) throw Error::kContract;
  }
  // GETMODULENAMESPACE names a module request. Map it once to the unique
  // namespace import entry for that request; an absent or repeated entry has
  // no single source binding. The canonical read is the appended external
  // slot that ModuleDataAccessor::EnumerateImportEntry assigns to the entry.
  void NamespaceRequests() {
    if (namespace_ready_) return;
    namespace_ready_ = true;
    constexpr std::uint32_t kAbsent = std::numeric_limits<std::uint32_t>::max(),
                            kRepeated = kAbsent - 1;
    if (module_->namespace_imports.size() >= kRepeated) throw Error::kResourceLimit;
    Admit(module_->requests.size(), sizeof(std::uint32_t));
    namespace_by_request_.assign(module_->requests.size(), kAbsent);
    for (std::size_t i = 0; i < module_->namespace_imports.size(); ++i) {
      Step();
      const auto request = module_->namespace_imports[i].request.value;
      if (request >= namespace_by_request_.size()) throw Error::kRequest;
      auto& entry = namespace_by_request_[request];
      entry = entry == kAbsent ? static_cast<std::uint32_t>(i) : kRepeated;
    }
  }
  ModuleReadTarget Target(analysis::ValueModuleRead& read) {
    if (read.request_namespace) {
      if (!ModuleNamespaces(contract_)) throw Error::kContract;
      if (read.space != analysis::ModuleSpace::kExternal ||
          read.loading != analysis::ModuleLoading::kOrdinary ||
          read.domain != analysis::ModuleDomain::kOrdinary) throw Error::kBinding;
      NamespaceRequests();
      if (read.slot.value >= namespace_by_request_.size()) throw Error::kRequest;
      const auto index = namespace_by_request_[read.slot.value];
      if (index >= module_->namespace_imports.size()) throw Error::kBinding;
      const auto canonical = module_->imports.size() + index;
      if (canonical > std::numeric_limits<std::uint16_t>::max()) throw Error::kSlot;
      read.slot = {static_cast<std::uint16_t>(canonical)};
      read.request_namespace = false;
    }
    const std::size_t slot = read.slot.value;
    switch (read.domain) {
      case analysis::ModuleDomain::kOrdinary: break;
      case analysis::ModuleDomain::kSendable:
        if (SendableOutput(contract_) != SendableOutputPolicy::kArkDirectiveV1) throw Error::kContract;
        // Sendable reads resolve the same module record: an external import
        // (ordinary or lazy) or a local binding of this module.
        if (read.space == analysis::ModuleSpace::kLocal && read.loading != analysis::ModuleLoading::kOrdinary)
          throw Error::kBinding;
        break;
    }
    switch (read.space) {
      case analysis::ModuleSpace::kLocal:
        if (read.loading != analysis::ModuleLoading::kOrdinary) throw Error::kBinding;
        if (slot >= locals_.size()) throw Error::kSlot;
        return LocalModuleBinding{locals_[slot]};
      case analysis::ModuleSpace::kExternal:
        // ModuleDataAccessor::EnumerateImportEntry appends namespace entries.
        if (slot < module_->imports.size()) {
          const auto& entry = module_->imports[slot];
          Loading(read, entry.request);
          return ImportedModuleBinding{entry.request, Request(entry.request), entry.local, entry.imported};
        }
        if (slot - module_->imports.size() >= module_->namespace_imports.size()) throw Error::kSlot;
        const auto& entry = module_->namespace_imports[slot - module_->imports.size()];
        Loading(read, entry.request);
        if (read.loading == analysis::ModuleLoading::kLazy) throw Error::kModuleKind;
        return NamespaceModuleBinding{entry.request, Request(entry.request), entry.local};
    }
    throw Error::kSlot;
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::ModuleInfo* module_;
  const analysis::ValueProgram& graph_;
  analysis::SemanticBudget budget_;
  std::size_t bytes_;
  std::uint64_t work_;
  SourceContract contract_;
  ModuleReads result_;
  std::vector<abc::EntityOffset> locals_;
  std::vector<std::uint32_t> namespace_by_request_;
  bool namespace_ready_ = false;
};
}  // namespace
std::variant<ModuleReads, ModuleReadFailure> ResolveModuleReads(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::ModuleInfo* module, const analysis::ValueProgram& graph,
    analysis::SemanticBudget budget, SourceContract contract) {
  try { return ReadResolver(file, header, module, graph, budget, contract).Resolve();
  } catch (const ModuleReadFailure& error) { return error;
  } catch (Error error) { return ModuleReadFailure{error};
  } catch (abc::MetadataError error) { return ModuleReadFailure{error};
  } catch (const std::bad_alloc&) { return ModuleReadFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return ModuleReadFailure{Error::kResourceLimit}; }
}
}  // namespace abcd::source
