#include "import_names.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "abcd/source/global_reference.h"
#include "runtime_name.h"

namespace abcd::source {
std::variant<ImportNames, ModuleImportFailure> RecoverImportNames(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::ModuleInfo& module, std::span<const abc::MethodInfo> methods,
    std::size_t bytes, abc::StringBudget strings) {
  const auto resource = [] {
    return ModuleImportFailure{ModuleImportError::kResourceLimit, {}, {}};
  };
  try {
    const auto charge = [&](std::size_t count, std::size_t width) {
      if (count > bytes / width) throw resource();
      bytes -= count * width;
    };
    ImportNames result;
    const auto count = module.imports.size() + module.namespace_imports.size();
    if (count < module.imports.size()) return resource();
    if (count > bytes / sizeof(std::string)) return resource();
    result.slots.resize(count);
    charge(result.slots.capacity(), sizeof(std::string));
    std::vector<std::string_view> callable_names;
    if (methods.size() > bytes / sizeof(std::string_view)) return resource();
    callable_names.reserve(methods.size());
    charge(callable_names.capacity(), sizeof(std::string_view));
    for (const auto& method : methods)
      if (const auto name = RuntimeName(method); name && !name->empty())
        callable_names.push_back(*name);
    std::sort(callable_names.begin(), callable_names.end());
    for (std::size_t slot = 0; slot < count; ++slot) {
      const auto offset = slot < module.imports.size() ? module.imports[slot].local
          : module.namespace_imports[slot - module.imports.size()].local;
      const auto parsed = abc::ReadStringEntity(file, header, offset,
          {std::min(strings.bytes, bytes)});
      if (const auto* error = std::get_if<abc::MetadataError>(&parsed))
        return ModuleImportFailure{*error == abc::MetadataError::kResourceLimit
            ? ModuleImportError::kResourceLimit : ModuleImportError::kString, offset, *error};
      const auto& name = std::get<std::u16string>(parsed);
      // Reuse the strict ASCII identifier/reserved-name boundary. Never use
      // imported names as a proof about globals or the host implementation.
      if (!AllowsGlobalReference(name, {})) continue;
      auto& target = result.slots[slot];
      const auto scratch = name.capacity() * sizeof(char16_t);
      if (scratch > bytes || name.size() >= bytes - scratch) return resource();
      target.assign(name.begin(), name.end());
      if (target.capacity() >= bytes - scratch) return resource();
      charge(target.capacity() + 1, sizeof(char));
      if (std::binary_search(callable_names.begin(), callable_names.end(),
                             std::string_view{target})) target.clear();
    }
    std::vector<std::size_t> order;
    if (count > bytes / sizeof(std::size_t)) return resource();
    order.reserve(count);
    charge(order.capacity(), sizeof(std::size_t));
    for (std::size_t slot = 0; slot < count; ++slot)
      if (!result.slots[slot].empty()) order.push_back(slot);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
      return result.slots[a] < result.slots[b];
    });
    for (std::size_t first = 0; first < order.size();) {
      auto last = first + 1;
      while (last < order.size() && result.slots[order[first]] == result.slots[order[last]]) ++last;
      if (last - first > 1)
        for (auto i = first; i < last; ++i) result.slots[order[i]].clear();
      first = last;
    }
    if (count > bytes / sizeof(std::string_view)) return resource();
    result.sorted.reserve(count);
    charge(result.sorted.capacity(), sizeof(std::string_view));
    for (const auto& name : result.slots)
      if (!name.empty()) result.sorted.push_back(name);
    std::sort(result.sorted.begin(), result.sorted.end());
    return result;
  } catch (const ModuleImportFailure& error) {
    return error;
  } catch (const std::bad_alloc&) {
    return resource();
  } catch (const std::length_error&) {
    return resource();
  }
}
}  // namespace abcd::source
