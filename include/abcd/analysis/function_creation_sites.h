#ifndef ABCD_ANALYSIS_FUNCTION_CREATION_SITES_H_
#define ABCD_ANALYSIS_FUNCTION_CREATION_SITES_H_

#include "abcd/abc/method_target.h"
#include "abcd/analysis/function_creation.h"
#include "abcd/analysis/semantic_ir.h"

namespace abcd::analysis {
struct FunctionSiteId {
  std::uint32_t value = 0;
  auto operator<=>(const FunctionSiteId&) const = default;
};
struct FunctionSiteRange { FunctionSiteId first; std::uint32_t count = 0; };
struct FunctionCreationSite {
  abc::EntityOffset target;
  abc::EntityOffset creator;
  abc::CodeOffset pc;
  FunctionCreation creation;
  abc::MethodTarget resolved;
};
enum class FunctionSiteError : std::uint8_t {
  kMalformedCreation, kResourceLimit, kWorkLimit
};
struct FunctionSiteFailure {
  std::variant<FunctionSiteError, abc::MetadataError, abc::BodyFailure,
               abc::MethodTargetError, abc::MethodAttributeError> error;
  std::optional<abc::EntityOffset> creator;
  std::optional<abc::CodeOffset> pc;
};
// Immutable occurrences of modern DEFINEFUNC/DEFINEMETHOD, resolved in each
// caller's pool region. Includes unreachable sites: reachability, runtime kind,
// home object, captures and child publication require separate owner proofs.
// Never collapse two sites referring to the same target into one closure.
// Same unchanged file/header/index/lookup precondition as ClassMethodSites.
class FunctionCreationSites {
 public:
  FunctionCreationSites(FunctionCreationSites&&) noexcept = default;
  FunctionCreationSites& operator=(FunctionCreationSites&&) noexcept = default;
  [[nodiscard]] static std::variant<FunctionCreationSites, FunctionSiteFailure> Build(
      const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      SemanticBudget budget = {});
  [[nodiscard]] FunctionSiteRange Find(abc::EntityOffset method) const;
  // Exact occurrence lookup in the same target/caller/PC index. Family readers
  // must not scan all uses of a shared target for every creation.
  [[nodiscard]] std::optional<FunctionSiteId> FindOccurrence(abc::EntityOffset target,
      abc::EntityOffset creator, abc::CodeOffset pc) const;
  [[nodiscard]] std::size_t Size() const { return sites_.size(); }
  [[nodiscard]] const FunctionCreationSite* At(FunctionSiteId site) const {
    return site.value < sites_.size() ? &sites_[site.value] : nullptr;
  }
  [[nodiscard]] std::size_t AllocatedBytes() const {
    return sizeof(*this) + sites_.capacity() * sizeof(FunctionCreationSite);
  }
  [[nodiscard]] std::uint64_t Work() const { return work_; }
 private:
  FunctionCreationSites() = default;
  std::vector<FunctionCreationSite> sites_;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::analysis
#endif
