#include "abcd/analysis/function_creation_sites.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>

#include "abcd/analysis/object_creation.h"
#include "abcd/corpus/abc_file.h"

namespace abcd::analysis {
namespace {
using Error = FunctionSiteError;
class SiteScan {
 public:
  explicit SiteScan(SemanticBudget budget) : remaining_(budget) {}
  [[noreturn]] void Fail(Error error) const { throw FunctionSiteFailure{error, creator_, pc_}; }
  void Step(std::uint64_t count = 1) {
    if (count > remaining_.work) Fail(Error::kWorkLimit);
    remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    if (count > remaining_.bytes / width) Fail(Error::kResourceLimit);
    remaining_.bytes -= count * width;
  }
  template <class T, class... Alternatives> T Take(std::variant<Alternatives...> result) const {
    if (auto* value = std::get_if<T>(&result)) return std::move(*value);
    std::visit([&](const auto& error) {
      if constexpr (!std::is_same_v<std::decay_t<decltype(error)>, T>)
        throw FunctionSiteFailure{error, creator_, pc_};
    }, result);
    Fail(Error::kMalformedCreation);
  }
  // Visits (caller, pc, creation, literal target, property, tag). Instruction
  // creations carry no literal target. Unreadable or non-object-shaped literals
  // are skipped: ReadObjectBuffer independently refuses them at their creation.
  template <class Visit> void Scan(const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, Visit visit) {
    for (const auto& method : index.methods) {
      creator_ = method.offset; pc_.reset(); Step();
      if (!method.code) continue;
      Step(method.code->instruction_bytes);
      const auto body = Take<abc::MethodBody>(abc::DecodeMethodBody(
          file, header.profile, *method.code, {remaining_.bytes}));
      for (const auto& instruction : body.instructions) {
        pc_ = instruction.offset; Step();
        const auto object = LiftObjectCreation(instruction.decoded);
        if (const auto* buffered = std::get_if<BufferedObjectCreation>(&object)) {
          Literal(file, header, index, method.offset, instruction.offset, buffered->literal, visit);
          continue;
        }
        const auto lifted = LiftFunctionCreation(instruction.decoded);
        const auto* creation = std::get_if<FunctionCreation>(&lifted);
        if (!creation) {
          if (std::get<CreationError>(lifted) != CreationError::kUnsupportedOpcode)
            Fail(Error::kMalformedCreation);
          continue;
        }
        visit(method.offset, instruction.offset, *creation, std::optional<abc::EntityOffset>{},
              kNoLiteralProperty, abc::LiteralTag::kNull);
      }
    }
    creator_.reset(); pc_.reset();
  }
  template <class Visit> void Literal(const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, abc::EntityOffset caller, abc::CodeOffset pc,
      abc::PoolIndex slot, Visit& visit) {
    const auto reference = abc::ResolvePoolReference(file, header, index, caller, slot,
                                                     abc::PoolEntityKind::kLiteralArray);
    if (const auto* error = std::get_if<abc::MetadataError>(&reference)) {
      if (*error == abc::MetadataError::kResourceLimit) Fail(Error::kResourceLimit);
      if (*error == abc::MetadataError::kFileChanged) throw FunctionSiteFailure{*error, creator_, pc_};
      return;
    }
    const auto read = abc::ReadLiteralArray(file, header, std::get<abc::PoolReference>(reference).offset,
                                            {remaining_.bytes});
    const auto* literal = std::get_if<abc::LiteralArray>(&read);
    if (!literal) {
      const auto kind = std::get<abc::LiteralFailure>(read).kind;
      if (kind == abc::LiteralError::kResourceLimit) Fail(Error::kResourceLimit);
      if (kind == abc::LiteralError::kFileChanged)
        throw FunctionSiteFailure{abc::MetadataError::kFileChanged, creator_, pc_};
      return;
    }
    Step(literal->entries.size());
    const auto& entries = literal->entries;
    std::uint32_t property = 0;
    for (std::size_t at = 0; at < entries.size(); ++property) {
      // v7 ExtractObjectDatas: a key followed by one data entry, or by a
      // METHOD-family entry whose METHOD_AFFILIATE creates the function.
      if (at + 1 >= entries.size() || property == kNoLiteralProperty) return;
      const auto& value = entries[at + 1];
      const bool method = value.tag == abc::LiteralTag::kMethod ||
          value.tag == abc::LiteralTag::kGeneratorMethod ||
          value.tag == abc::LiteralTag::kAsyncGeneratorMethod;
      if (!method) { at += 2; continue; }
      if (at + 2 >= entries.size() || entries[at + 2].tag != abc::LiteralTag::kMethodAffiliate) return;
      const auto* target = std::get_if<abc::EntityOffset>(&value.value);
      const auto* length = std::get_if<abc::LiteralBits>(&entries[at + 2].value);
      at += 3;
      if (!target || !length || length->value > UINT8_MAX) continue;
      const FunctionCreation creation{CreationKind::kMethod, {}, {static_cast<std::uint8_t>(length->value)},
                                      HomeObjectInput::kLiteralObject, 0};
      visit(caller, pc, creation, std::optional<abc::EntityOffset>{*target}, property, value.tag);
    }
  }
  std::size_t Bytes() const { return remaining_.bytes; }
  std::uint64_t Work() const { return remaining_.work; }
 private:
  SemanticBudget remaining_;
  std::optional<abc::EntityOffset> creator_;
  std::optional<abc::CodeOffset> pc_;
};
}  // namespace

FunctionSiteRange FunctionCreationSites::Find(abc::EntityOffset method) const {
  const auto first = std::lower_bound(sites_.begin(), sites_.end(), method,
      [](const auto& site, auto target) { return site.target < target; });
  const auto end = std::upper_bound(first, sites_.end(), method,
      [](auto target, const auto& site) { return target < site.target; });
  return {{static_cast<std::uint32_t>(first - sites_.begin())},
          static_cast<std::uint32_t>(end - first)};
}
std::optional<FunctionSiteId> FunctionCreationSites::FindOccurrence(
    abc::EntityOffset target, abc::EntityOffset creator, abc::CodeOffset pc,
    std::uint32_t property) const {
  const auto key = std::tuple{target, creator, pc, property};
  const auto found = std::lower_bound(sites_.begin(), sites_.end(), key,
      [](const auto& site, const auto& wanted) {
        return std::tuple{site.target, site.creator, site.pc, site.property} < wanted;
      });
  if (found == sites_.end() ||
      std::tuple{found->target, found->creator, found->pc, found->property} != key) return {};
  return FunctionSiteId{static_cast<std::uint32_t>(found - sites_.begin())};
}
std::span<const FunctionSiteId> FunctionCreationSites::FindLiteral(
    abc::EntityOffset creator, abc::CodeOffset pc) const {
  const auto key = std::tuple{creator, pc};
  const auto less = [&](FunctionSiteId id) {
    const auto& site = sites_[id.value]; return std::tuple{site.creator, site.pc};
  };
  const auto first = std::lower_bound(literals_.begin(), literals_.end(), key,
      [&](FunctionSiteId id, const auto& wanted) { return less(id) < wanted; });
  const auto end = std::upper_bound(first, literals_.end(), key,
      [&](const auto& wanted, FunctionSiteId id) { return wanted < less(id); });
  return {literals_.data() + (first - literals_.begin()), static_cast<std::size_t>(end - first)};
}
std::variant<FunctionCreationSites, FunctionSiteFailure> FunctionCreationSites::Build(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    SemanticBudget allowance) {
  try {
    if (header.kind != abc::AbcKind::kDynamic || !std::holds_alternative<abc::DynamicLayout>(header.layout))
      return FunctionSiteFailure{abc::MetadataError::kUnsupportedProfile, {}, {}};
    if (file.Size() != header.file_size || file.CheckUnchanged())
      return FunctionSiteFailure{abc::MetadataError::kFileChanged, {}, {}};
    SiteScan scan(allowance);
    scan.Admit(1, sizeof(FunctionCreationSites) + sizeof(SiteScan));
    std::size_t count = 0;
    scan.Scan(file, header, index, [&](auto, auto, const auto&, auto, auto, auto) {
      if (count >= std::numeric_limits<std::uint32_t>::max() ||
          count >= scan.Bytes() / sizeof(FunctionCreationSite)) scan.Fail(Error::kResourceLimit);
      ++count;
    });
    // Count then reserve once: retained occurrence storage is admitted before
    // the second bounded decode. No growing vector overlaps uncharged bodies.
    FunctionCreationSites result;
    scan.Admit(count, sizeof(FunctionCreationSite));
    result.sites_.reserve(count);
    scan.Admit(result.sites_.capacity() - count, sizeof(FunctionCreationSite));
    // ResolveMethodTarget looks up both caller and target, and resolves one
    // regional pool entry. Charge every binary-search comparison upper bound.
    constexpr std::size_t kMethodLookups = 2;
    const auto lookup_work = std::bit_width(index.regions.size()) +
        kMethodLookups * std::bit_width(index.methods.size()) + 1;
    scan.Scan(file, header, index, [&](auto caller, auto pc, const auto& creation,
                                       std::optional<abc::EntityOffset> literal,
                                       std::uint32_t property, abc::LiteralTag tag) {
      if (result.sites_.size() >= count)
        throw FunctionSiteFailure{abc::MetadataError::kFileChanged, caller, pc};
      scan.Step(lookup_work);
      const auto target = scan.Take<abc::MethodTarget>(literal
          ? abc::ResolveLiteralMethodTarget(index, header, lookup, caller, *literal)
          : abc::ResolveMethodTarget(file, header, index, lookup, caller, creation.method));
      result.sites_.push_back({index.methods[target.position.value].offset, caller, pc, creation, target,
                               property, tag});
    });
    if (result.sites_.size() != count || file.CheckUnchanged())
      return FunctionSiteFailure{abc::MetadataError::kFileChanged, {}, {}};
    std::sort(result.sites_.begin(), result.sites_.end(), [&](const auto& a, const auto& b) {
      scan.Step();
      return std::tuple{a.target, a.creator, a.pc, a.property} <
             std::tuple{b.target, b.creator, b.pc, b.property};
    });
    std::size_t literals = 0;
    for (const auto& site : result.sites_) { scan.Step(); literals += site.property != kNoLiteralProperty; }
    scan.Admit(literals, sizeof(FunctionSiteId));
    result.literals_.reserve(literals);
    scan.Admit(result.literals_.capacity() - literals, sizeof(FunctionSiteId));
    for (std::size_t i = 0; i < result.sites_.size(); ++i) {
      scan.Step();
      if (result.sites_[i].property != kNoLiteralProperty)
        result.literals_.push_back({static_cast<std::uint32_t>(i)});
    }
    std::sort(result.literals_.begin(), result.literals_.end(), [&](auto a, auto b) {
      scan.Step();
      const auto& x = result.sites_[a.value]; const auto& y = result.sites_[b.value];
      return std::tuple{x.creator, x.pc, x.property} < std::tuple{y.creator, y.pc, y.property};
    });
    result.work_ = allowance.work - scan.Work();
    return result;
  } catch (const FunctionSiteFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FunctionSiteFailure{Error::kResourceLimit, {}, {}};
  } catch (const std::length_error&) { return FunctionSiteFailure{Error::kResourceLimit, {}, {}}; }
}
}  // namespace abcd::analysis
