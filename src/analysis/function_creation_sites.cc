#include "abcd/analysis/function_creation_sites.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>

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
        const auto lifted = LiftFunctionCreation(instruction.decoded);
        const auto* creation = std::get_if<FunctionCreation>(&lifted);
        if (!creation) {
          if (std::get<CreationError>(lifted) != CreationError::kUnsupportedOpcode)
            Fail(Error::kMalformedCreation);
          continue;
        }
        visit(method.offset, instruction.offset, *creation);
      }
    }
    creator_.reset(); pc_.reset();
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
    abc::EntityOffset target, abc::EntityOffset creator, abc::CodeOffset pc) const {
  const auto key = std::tuple{target, creator, pc};
  const auto found = std::lower_bound(sites_.begin(), sites_.end(), key,
      [](const auto& site, const auto& wanted) {
        return std::tuple{site.target, site.creator, site.pc} < wanted;
      });
  if (found == sites_.end() || std::tuple{found->target, found->creator, found->pc} != key) return {};
  return FunctionSiteId{static_cast<std::uint32_t>(found - sites_.begin())};
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
    scan.Scan(file, header, index, [&](auto, auto, const auto&) {
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
    scan.Scan(file, header, index, [&](auto caller, auto pc, const auto& creation) {
      if (result.sites_.size() >= count)
        throw FunctionSiteFailure{abc::MetadataError::kFileChanged, caller, pc};
      scan.Step(lookup_work);
      const auto target = scan.Take<abc::MethodTarget>(abc::ResolveMethodTarget(
          file, header, index, lookup, caller, creation.method));
      result.sites_.push_back({index.methods[target.position.value].offset, caller, pc, creation, target});
    });
    if (result.sites_.size() != count || file.CheckUnchanged())
      return FunctionSiteFailure{abc::MetadataError::kFileChanged, {}, {}};
    std::sort(result.sites_.begin(), result.sites_.end(), [&](const auto& a, const auto& b) {
      scan.Step();
      return std::tuple{a.target, a.creator, a.pc} < std::tuple{b.target, b.creator, b.pc};
    });
    result.work_ = allowance.work - scan.Work();
    return result;
  } catch (const FunctionSiteFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FunctionSiteFailure{Error::kResourceLimit, {}, {}};
  } catch (const std::length_error&) { return FunctionSiteFailure{Error::kResourceLimit, {}, {}}; }
}
}  // namespace abcd::analysis
