#include "module_binding_name.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "abcd/abc/string_entity.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/scalar_expression.h"

namespace abcd::source {
namespace {
using Error = ModuleBindingNameError;
// Output-only namespace. One scan measures its longest existing '$' run, so
// choosing a disjoint prefix cannot require repeated whole-inventory searches.
constexpr std::u16string_view kAnonymousPrefix = u"$default";
}
std::variant<ModuleBindingNames, ModuleBindingNameFailure> ResolveModuleBindingNames(
    const corpus::AbcFile& file, const abc::Header& header, const abc::ModuleInfo& module,
    analysis::SemanticBudget budget) {
  try {
    auto bytes = budget.bytes;
    auto work = budget.work;
    const auto step = [&](std::size_t count = 1) {
      if (count > work) throw Error::kWorkLimit;
      work -= count;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      if (count > bytes / width) throw Error::kResourceLimit;
      bytes -= count * width;
    };
    const auto reserve = [&]<class T>(std::vector<T>& values, std::size_t count) {
      admit(count, sizeof(T)); values.reserve(count);
      admit(values.capacity() - count, sizeof(T));
    };
    if (file.Size() != header.file_size || file.CheckUnchanged()) throw abc::MetadataError::kFileChanged;
    ModuleBindingNames result;
    reserve(result.locals, module.local_exports.size());
    reserve(result.evidence, module.local_exports.size());
    reserve(result.rows, module.local_exports.size());
    std::vector<abc::EntityOffset> entities;
    const auto add = [&](abc::EntityOffset entity) { step(); entities.push_back(entity); };
    const auto imports = module.imports.size();
    if (module.namespace_imports.size() > bytes / sizeof(abc::EntityOffset) ||
        imports > bytes / sizeof(abc::EntityOffset) - module.namespace_imports.size())
      throw Error::kResourceLimit;
    const auto external = imports + module.namespace_imports.size();
    if (module.local_exports.size() > bytes / sizeof(abc::EntityOffset) ||
        external > bytes / sizeof(abc::EntityOffset) - module.local_exports.size())
      throw Error::kResourceLimit;
    reserve(entities, external + module.local_exports.size());
    for (const auto& entry : module.imports) add(entry.local);
    for (const auto& entry : module.namespace_imports) add(entry.local);
    for (const auto& entry : module.local_exports) add(entry.local);
    std::sort(entities.begin(), entities.end(), [&](auto a, auto b) { step(); return a < b; });
    entities.erase(std::unique(entities.begin(), entities.end(), [&](auto a, auto b) {
      step(); return a == b;
    }), entities.end());
    std::vector<std::u16string> decoded;
    reserve(decoded, entities.size());
    std::size_t dollars = 0;
    const auto observe = [&](std::u16string_view text) {
      step(text.size() + 1);
      if (!text.starts_with(kAnonymousPrefix)) return;
      std::size_t count = 0;
      while (kAnonymousPrefix.size() + count < text.size() &&
          text[kAnonymousPrefix.size() + count] == u'$') ++count;
      dollars = std::max(dollars, count);
    };
    for (const auto entity : entities) {
      step();
      auto parsed = abc::ReadStringEntity(file, header, entity, {bytes});
      if (const auto* error = std::get_if<abc::MetadataError>(&parsed)) throw *error;
      auto text = std::get<std::u16string>(std::move(parsed));
      admit(text.capacity() + 1, sizeof(char16_t));
      observe(text);
      decoded.push_back(std::move(text));
    }
    // Content equality is interned once as well: two distinct long-name
    // entities alternating in many alias rows must not rescan their text.
    std::vector<std::size_t> order, classes;
    reserve(order, decoded.size()); reserve(classes, decoded.size());
    for (std::size_t i = 0; i < decoded.size(); ++i) { order.push_back(i); classes.push_back(0); }
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
      step(std::min(decoded[a].size(), decoded[b].size()) + 1);
      return decoded[a] < decoded[b];
    });
    std::size_t group = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
      step();
      if (i) {
        const auto& a = decoded[order[i - 1]];
        const auto& b = decoded[order[i]];
        step(std::min(a.size(), b.size()) + 1);
        if (a != b) ++group;
      }
      classes[order[i]] = group;
    }
    std::optional<std::size_t> previous;
    bool anonymous = false;
    for (const auto& entry : module.local_exports) {
      const auto found = std::lower_bound(entities.begin(), entities.end(), entry.local,
          [&](auto a, auto b) { step(); return a < b; });
      const auto id = static_cast<std::size_t>(found - entities.begin());
      const auto& text = decoded[id];
      const bool equal = previous && classes[*previous] == classes[id];
      if (!equal) {
        const bool generated = text == kAnonymousDefaultLocal;
        step(text.size() + 1);
        if (!generated && !IsSourceIdentifier(std::u16string_view(text))) throw Error::kName;
        result.evidence.push_back(generated ? NameEvidence::kSynthesized : NameEvidence::kMetadata);
        anonymous |= generated;
        admit(text.size() + 1, sizeof(char16_t));
        auto& copy = result.locals.emplace_back(text);
        admit(copy.capacity() - text.size(), sizeof(char16_t));
      }
      previous = id;
      result.rows.push_back(result.locals.size() - 1);
    }
    if (anonymous) {
      // max+1 '$' makes this entire prefix absent from all metadata bindings.
      // Append the VM slot to distinguish nonadjacent equal sentinel rows.
      const auto prefix_size = kAnonymousPrefix.size() + dollars + 1;
      admit(prefix_size + 1, sizeof(char16_t));
      std::u16string prefix;
      prefix.reserve(prefix_size);
      admit(prefix.capacity() - prefix_size, sizeof(char16_t));
      prefix.append(kAnonymousPrefix);
      prefix.append(dollars + 1, u'$');
      for (std::size_t i = 0; i < result.locals.size(); ++i) {
        step();
        if (result.evidence[i] != NameEvidence::kSynthesized) continue;
        const auto number = std::to_string(i);
        const auto size = prefix.size() + number.size();
        admit(size + 1, sizeof(char16_t));
        std::u16string name;
        name.reserve(size);
        admit(name.capacity() - size, sizeof(char16_t));
        name.append(prefix);
        name.append(number.begin(), number.end());
        step(name.size()); result.locals[i] = std::move(name);
      }
    }
    if (file.CheckUnchanged()) throw abc::MetadataError::kFileChanged;
    result.allocated_bytes = budget.bytes - bytes;
    result.work = budget.work - work;
    return result;
  } catch (Error error) { return ModuleBindingNameFailure{error};
  } catch (abc::MetadataError error) { return ModuleBindingNameFailure{error};
  } catch (const std::bad_alloc&) { return ModuleBindingNameFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return ModuleBindingNameFailure{Error::kResourceLimit}; }
}
}  // namespace abcd::source
