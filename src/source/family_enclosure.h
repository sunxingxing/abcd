#ifndef ABCD_SOURCE_FAMILY_ENCLOSURE_H_
#define ABCD_SOURCE_FAMILY_ENCLOSURE_H_

#include "function_family.h"
#include "abcd/source/global_reference.h"

namespace abcd::source {
enum class FamilyEnclosureError : std::uint8_t {
  kLayout, kStructure, kShadowedGlobal, kResourceLimit, kWorkLimit,
  kMissingContext, kShadowedModule, kModuleBinding, kMissingModuleContext, kCallableText
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
  std::size_t module_reads = 0;
  std::size_t realm_reads = 0;
  std::size_t symbol_reads = 0;
  std::size_t argument_snapshots = 0;
};
// Checks actual creation-point lexical ancestry, including block scopes and
// TDZ bindings, not a union of all parent locals. Original trees remain intact.
// Requires fresh family layout/capture products and rechecks tree structure.
// Does NOT prove the external record/module/global enclosure, initialization,
// capture writes or strictness. A nonzero global_reads needs external checking.
// Euler scope intervals + sorted name intervals avoid per-read ancestor walks.
// Module aliases must agree across owners and cannot hide ancestor locals.
// Their synthetic scope is not a newly declared binding at child creation.
[[nodiscard]] std::variant<FamilyEnclosure, FamilyEnclosureFailure> VerifyFamilyEnclosure(
    const ConstructedFamily& family, analysis::SemanticBudget budget = {});
// Additionally checks the caller-proven complete destination enclosure. Null
// admits only a family without global/realm reads, never an assumed empty enclosure.
// Borrowed sorted indexes follow GlobalScope's contract; recheck on embedding.
// This does not establish module initialization, strictness or publication.
// Module reads currently refuse kMissingModuleContext after lexical checks;
// a GlobalScope alone is not a module-binding execution capability.
[[nodiscard]] std::variant<FamilyEnclosure, FamilyEnclosureFailure> VerifyFamilyDestination(
    const ConstructedFamily& family, const GlobalScope* scope,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
