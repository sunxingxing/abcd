#include "cell_names.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include "abcd/abc/literal_array.h"
#include "abcd/source/global_reference.h"
#include "abcd/corpus/abc_file.h"
namespace abcd::source {
namespace {
using Error = TreeConstantError;
struct Site { analysis::OpId operation; abc::EntityOffset literal; std::uint16_t slots; };
struct Entry { std::uint16_t slot; abc::EntityOffset name; };
class Quota {
 public:
  explicit Quota(analysis::SemanticBudget budget) : initial(budget), left(budget) {}
  void Step(std::uint64_t n = 1) { if (n > left.work) throw Error::kWorkLimit; left.work -= n; }
  void Admit(std::size_t n, std::size_t width) {
    if (n > left.bytes / width) throw Error::kResourceLimit; left.bytes -= n * width;
  }
  template<class T> void Grow(std::vector<T>& values, std::size_t count) {
    if (count <= values.capacity()) return;
    const auto old = values.capacity();
    const auto capacity = std::max(count, old > values.max_size() / 2 ? values.max_size() : old * 2);
    Admit(capacity, sizeof(T)); values.reserve(capacity);
    Admit(values.capacity() - capacity, sizeof(T)); left.bytes += old * sizeof(T);
  }
  analysis::SemanticBudget initial, left;
};
}
std::variant<CellNameStorage, TreeConstantError, abc::MetadataError> ResolveCellNames(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    abc::EntityOffset method, const analysis::ValueProgram& graph,
    std::vector<std::u16string>& names, std::vector<LocalNameSyntax>& hints, analysis::SemanticBudget budget) {
  try {
    Quota quota(budget); std::vector<Site> sites;
    if (graph.ops.size() >= UINT32_MAX) return Error::kResourceLimit;
    if (file.CheckUnchanged()) return abc::MetadataError::kFileChanged;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      quota.Step(); const auto& op = graph.ops[i];
      const auto* push = std::get_if<analysis::ValueEnvironmentPush>(&op.operation);
      if (op.life != analysis::ValueLife::kLive || !push || !push->scope) continue;
      quota.Step(std::max<std::size_t>(1, std::bit_width(index.regions.size())));
      const auto target = abc::ResolvePoolReference(file, header, index, method, *push->scope, abc::PoolEntityKind::kLiteralArray);
      if (const auto* error = std::get_if<abc::MetadataError>(&target)) return *error;
      quota.Grow(sites, sites.size() + 1);
      sites.push_back({{static_cast<std::uint32_t>(i)}, std::get<abc::PoolReference>(target).offset, push->slots});
    }
    std::sort(sites.begin(), sites.end(), [&](const auto& a, const auto& b) { quota.Step(); return a.literal < b.literal; });
    for (std::size_t first = 0; first < sites.size();) {
      auto end = first + 1; auto slots = sites[first].slots;
      while (end < sites.size() && sites[end].literal == sites[first].literal) {
        quota.Step(); slots = std::min(slots, sites[end++].slots);
      }
      auto read = abc::ReadLiteralArray(file, header, sites[first].literal, {quota.left.bytes});
      if (const auto* error = std::get_if<abc::LiteralFailure>(&read))
        return error->kind == abc::LiteralError::kResourceLimit ? Error::kResourceLimit : Error::kLexicalName;
      const auto& values = std::get<abc::LiteralArray>(read).entries;
      const auto literal_bytes = values.capacity() * sizeof(abc::LiteralEntry);
      quota.Admit(literal_bytes, 1);
      constexpr std::size_t kPairEntries = 2;
      if (values.empty() || values.size() % kPairEntries != 1 || values.front().tag != abc::LiteralTag::kInteger)
        return Error::kLexicalName;
      std::vector<Entry> entries; quota.Grow(entries, (values.size() - 1) / kPairEntries);
      for (std::size_t i = 1; i < values.size(); i += kPairEntries) {
        quota.Step(kPairEntries);
        if (values[i].tag != abc::LiteralTag::kString || values[i + 1].tag != abc::LiteralTag::kInteger)
          return Error::kLexicalName;
        const auto slot = std::get<abc::LiteralBits>(values[i + 1].value).value;
        if (slot >= slots) return Error::kLexicalName;
        entries.push_back({static_cast<std::uint16_t>(slot), std::get<abc::EntityOffset>(values[i].value)});
      }
      std::sort(entries.begin(), entries.end(), [&](const auto& a, const auto& b) { quota.Step(); return a.slot < b.slot; });
      for (std::size_t i = 0; i < entries.size();) {
        auto next = i + 1;
        while (next < entries.size() && entries[next].slot == entries[i].slot) { quota.Step(); ++next; }
        if (next != i + 1) { i = next; continue; }
        auto text = abc::ReadStringEntity(file, header, entries[i].name, {quota.left.bytes});
        if (const auto* error = std::get_if<abc::MetadataError>(&text)) return *error;
        const auto& name = std::get<std::u16string>(text);
        const auto text_bytes = (name.capacity() + 1) * sizeof(char16_t); quota.Admit(text_bytes, 1);
        quota.Step(name.size());
        if (name != u"globalThis" && AllowsGlobalReference(name, {})) for (auto site = first; site < end; ++site) {
          quota.Step(name.size() + 1);
          if (names.size() >= UINT32_MAX) return Error::kResourceLimit;
          quota.Grow(names, names.size() + 1); quota.Grow(hints, hints.size() + 1);
          quota.Admit(name.size() + 1, sizeof(char16_t));
          const NameId id{static_cast<std::uint32_t>(names.size())}; names.push_back(name);
          quota.Admit(names.back().capacity() - name.size(), sizeof(char16_t));
          hints.push_back({sites[site].operation, {entries[i].slot}, id});
        }
        quota.left.bytes += text_bytes; i = next;
      }
      quota.left.bytes += literal_bytes + entries.capacity() * sizeof(Entry); first = end;
    }
    std::sort(hints.begin(), hints.end(), [&](const auto& a, const auto& b) {
      quota.Step(); return std::pair{a.allocation.value,a.slot.value} < std::pair{b.allocation.value,b.slot.value};
    });
    if (file.CheckUnchanged()) return abc::MetadataError::kFileChanged;
    // Scratch is gone on return; output vectors and name payloads remain owned.
    quota.left.bytes += sites.capacity() * sizeof(Site);
    return CellNameStorage{budget.bytes - quota.left.bytes, budget.work - quota.left.work};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
