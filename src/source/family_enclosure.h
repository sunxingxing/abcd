#ifndef ABCD_SOURCE_FAMILY_ENCLOSURE_H_
#define ABCD_SOURCE_FAMILY_ENCLOSURE_H_

#include "function_family.h"

namespace abcd::source {
enum class FamilyEnclosureError : std::uint8_t {
  kLayout, kStructure, kShadowedGlobal, kResourceLimit, kWorkLimit
};
struct FamilyEnclosureFailure {
  FamilyEnclosureError kind;
  FamilyId member;
  std::optional<NameId> name;
};
struct FamilyEnclosure {
  std::size_t global_reads = 0;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Checks actual creation-point lexical ancestry, including block scopes and
// TDZ bindings, not a union of all parent locals. Original trees remain intact.
// Requires fresh family layout/capture products and rechecks tree structure.
// Does NOT prove the external record/module/global enclosure, initialization,
// capture writes or strictness. A nonzero global_reads needs external checking.
// Euler scope intervals + sorted name intervals avoid per-read ancestor walks.
[[nodiscard]] std::variant<FamilyEnclosure, FamilyEnclosureFailure> VerifyFamilyEnclosure(
    const ConstructedFamily& family, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
