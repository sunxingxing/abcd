#ifndef ABCD_SOURCE_PHASED_CREATIONS_H_
#define ABCD_SOURCE_PHASED_CREATIONS_H_

#include "phased_body.h"
#include "abcd/analysis/creation_binding.h"

namespace abcd::source {
// Value identity survives moving the resolved child context into its owner.
// Rebinding must use this original site, not a residual graph ordinal.
struct PhasedCreationSite {
  analysis::OpId operation;
  analysis::InstructionId instruction;
  analysis::CreationBinding binding;
};
struct PhasedCreation : PhasedCreationSite {
  CreationTreeContext context;
};
struct PhasedCreations {
  // One entry per live creation, in operation order. Repeated templates remain
  // distinct occurrences; capture cells and complete child trees are NOT proved.
  std::vector<PhasedCreation> sites;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class PhasedCreationError : std::uint8_t {
  kIdentity, kOrigin, kOccurrence, kResourceLimit, kWorkLimit
};
struct PhasedCreationFailure {
  std::optional<analysis::OpId> operation;
  std::variant<PhasedCreationError, analysis::CreationError, analysis::CreationBindingError,
      abc::MethodTargetError, abc::MetadataError, abc::MethodAttributeError,
      CreationContextFailure> cause;
};
// Same unchanged file/index/roles/site index and original decoded body as the
// immutable parent. Retain the parent: captured environment IDs belong to it.
// Reuses original exception environment state, never a normal-CFG substitute.
// Budget covers additional results/scratch, not the caller-owned parent/index.
[[nodiscard]] std::variant<PhasedCreations, PhasedCreationFailure> BindPhasedCreations(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
    const PhasedBody& parent, const abc::MethodBody& body, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
