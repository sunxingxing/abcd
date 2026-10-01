#include "abcd/analysis/class_method_sites.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>

#include "abcd/analysis/class_creation.h"
#include "abcd/corpus/abc_file.h"

namespace abcd::analysis {
namespace {
struct ClassReference {
  abc::EntityOffset creator;
  abc::CodeOffset pc;
  abc::EntityOffset literal;
  ClassCreation creation;
};
template <class T, class... Alternatives>
T Take(std::variant<Alternatives...> result) {
  if (auto* value = std::get_if<T>(&result)) return std::move(*value);
  std::visit([](const auto& error) {
    if constexpr (!std::is_same_v<std::decay_t<decltype(error)>, T>) throw ClassSiteFailure{error};
  }, result);
  throw ClassSiteFailure{ClassSiteError::kMalformedCreation};
}
class SiteBudget {
 public:
  explicit SiteBudget(SemanticBudget budget) : remaining_(budget) {}
  void Step(std::uint64_t count = 1) {
    if (count > remaining_.work) throw ClassSiteFailure{ClassSiteError::kWorkLimit};
    remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    if (count > remaining_.bytes / width) throw ClassSiteFailure{ClassSiteError::kResourceLimit};
    remaining_.bytes -= count * width;
  }
  [[nodiscard]] std::size_t Bytes() const { return remaining_.bytes; }
  [[nodiscard]] std::uint64_t Work() const { return remaining_.work; }
 private:
  SemanticBudget remaining_;
};
template <class Visit>
void ScanClasses(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, SiteBudget& budget, Visit visit) {
  for (const auto& method : index.methods) {
    budget.Step();
    if (!method.code) continue;
    budget.Step(method.code->instruction_bytes);
    const auto body = Take<abc::MethodBody>(abc::DecodeMethodBody(file, header.profile, *method.code, {budget.Bytes()}));
    for (const auto& instruction : body.instructions) {
      budget.Step();
      const auto lifted = LiftClassCreation(instruction.decoded);
      const auto* creation = std::get_if<ClassCreation>(&lifted);
      if (!creation) {
        if (std::get<CreationError>(lifted) != CreationError::kUnsupportedOpcode)
          throw ClassSiteFailure{ClassSiteError::kMalformedCreation};
        continue;
      }
      const auto registers = static_cast<std::uint64_t>(method.code->registers) + method.code->arguments;
      if (creation->parent.value >= registers)
        throw ClassSiteFailure{ClassSiteError::kMalformedCreation};
      visit(method.offset, instruction.offset, *creation);
    }
  }
}
template <class Visit>
void ScanMembers(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    std::span<const ClassReference> references, SiteBudget& budget, Visit visit) {
  for (std::size_t first = 0; first < references.size();) {
    budget.Step();
    auto end = first + 1;
    while (end < references.size() && references[end].literal == references[first].literal) { budget.Step(); ++end; }
    const auto literal = Take<abc::LiteralArray>(abc::ReadLiteralArray(file, header, references[first].literal,
                                                                    {budget.Bytes()}));
    budget.Step(literal.entries.size());
    if (literal.entries.capacity() > budget.Bytes() / sizeof(abc::LiteralEntry))
      throw ClassSiteFailure{ClassSiteError::kResourceLimit};
    const auto bytes = budget.Bytes() - literal.entries.capacity() * sizeof(abc::LiteralEntry);
    const auto members = Take<abc::ClassBuffer>(abc::DecodeClassBuffer(literal, {bytes}));
    if (members.properties.size() > std::numeric_limits<std::uint32_t>::max())
      throw ClassSiteFailure{ClassSiteError::kResourceLimit};
    for (std::size_t p = 0; p < members.properties.size(); ++p) {
      budget.Step();
      const auto& property = members.properties[p];
      const auto* method = std::get_if<abc::ClassMethod>(&property.value);
      if (!method) continue;
      const auto target = lookup.Find(method->target);
      if (!target || target->value >= index.methods.size() || index.methods[target->value].offset != method->target)
        throw ClassSiteFailure{ClassSiteError::kTargetMissing};
      visit(references.subspan(first, end - first), static_cast<std::uint32_t>(p), property, *method);
    }
    first = end;
  }
}
}  // namespace

ClassSiteRange ClassMethodSites::Find(abc::EntityOffset method) const {
  const auto first = std::lower_bound(sites_.begin(), sites_.end(), method,
      [](const auto& site, auto target) { return site.target < target; });
  const auto end = std::upper_bound(first, sites_.end(), method,
      [](auto target, const auto& site) { return target < site.target; });
  return {{static_cast<std::uint32_t>(first - sites_.begin())}, static_cast<std::uint32_t>(end - first)};
}
std::optional<ClassSiteId> ClassMethodSites::FindOccurrence(abc::EntityOffset target,
    abc::EntityOffset creator, abc::CodeOffset pc, std::uint32_t property) const {
  const auto key = std::tuple{target, creator, pc, property};
  const auto found = std::lower_bound(sites_.begin(), sites_.end(), key,
      [](const auto& site, const auto& value) {
        return std::tuple{site.target, site.creator, site.pc, site.property} < value;
      });
  if (found == sites_.end() || std::tuple{found->target, found->creator, found->pc, found->property} != key)
    return {};
  return ClassSiteId{static_cast<std::uint32_t>(found - sites_.begin())};
}
std::variant<ClassMethodSites, ClassSiteFailure> ClassMethodSites::Build(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    SemanticBudget allowance) {
  try {
    if (header.kind != abc::AbcKind::kDynamic || !std::holds_alternative<abc::DynamicLayout>(header.layout))
      return ClassSiteFailure{abc::MetadataError::kUnsupportedProfile};
    if (file.Size() != header.file_size || file.CheckUnchanged())
      return ClassSiteFailure{abc::MetadataError::kFileChanged};
    SiteBudget budget(allowance);
    budget.Admit(1, sizeof(ClassMethodSites) + sizeof(std::vector<ClassReference>));
    std::size_t references_count = 0;
    ScanClasses(file, header, index, budget, [&](auto, auto, auto) {
      if (references_count >= budget.Bytes() / sizeof(ClassReference))
        throw ClassSiteFailure{ClassSiteError::kResourceLimit};
      ++references_count;
    });
    budget.Admit(references_count, sizeof(ClassReference));
    std::vector<ClassReference> references;
    references.reserve(references_count);
    budget.Admit(references.capacity() - references_count, sizeof(ClassReference));
    ScanClasses(file, header, index, budget, [&](auto method, auto pc, auto creation) {
      if (references.size() == references_count) throw ClassSiteFailure{abc::MetadataError::kFileChanged};
      const auto target = Take<abc::PoolReference>(abc::ResolvePoolReference(
          file, header, index, method, creation.literal, abc::PoolEntityKind::kLiteralArray));
      references.push_back({method, pc, target.offset, creation});
    });
    if (references.size() != references_count) throw ClassSiteFailure{abc::MetadataError::kFileChanged};
    std::sort(references.begin(), references.end(), [&](const auto& a, const auto& b) {
      budget.Step(); return std::tuple{a.literal, a.creator, a.pc} < std::tuple{b.literal, b.creator, b.pc};
    });
    // Decode each unique buffer twice: first admit the exact occurrence output,
    // then populate it. Reused buffers are never decoded per member or method.
    std::size_t count = 0;
    ScanMembers(file, header, index, lookup, references, budget,
        [&](auto sites, auto, const auto&, const auto&) {
          const auto limit = budget.Bytes() / sizeof(ClassMethodSite);
          if (sites.size() > limit - count ||
              sites.size() > std::numeric_limits<std::uint32_t>::max() - count)
            throw ClassSiteFailure{ClassSiteError::kResourceLimit};
          count += sites.size();
        });
    ClassMethodSites result;
    budget.Admit(count, sizeof(ClassMethodSite));
    result.sites_.reserve(count);
    budget.Admit(result.sites_.capacity() - count, sizeof(ClassMethodSite));
    ScanMembers(file, header, index, lookup, references, budget,
        [&](auto sites, auto property_index, const auto& property, const auto& method) {
          for (const auto& site : sites) {
            budget.Step();
            if (result.sites_.size() >= count) throw ClassSiteFailure{abc::MetadataError::kFileChanged};
            result.sites_.push_back({method.target, site.creator, site.pc, site.literal,
                                    property_index, property.key, method.kind, property.placement, method.length, site.creation});
          }
        });
    if (result.sites_.size() != count || file.CheckUnchanged())
      throw ClassSiteFailure{abc::MetadataError::kFileChanged};
    std::sort(result.sites_.begin(), result.sites_.end(), [&](const auto& a, const auto& b) {
      budget.Step();
      return std::tuple{a.target, a.creator, a.pc, a.property} < std::tuple{b.target, b.creator, b.pc, b.property};
    });
    result.work_ = allowance.work - budget.Work();
    return result;
  } catch (const ClassSiteFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return ClassSiteFailure{ClassSiteError::kResourceLimit};
  } catch (const std::length_error&) { return ClassSiteFailure{ClassSiteError::kResourceLimit}; }
}
}  // namespace abcd::analysis
