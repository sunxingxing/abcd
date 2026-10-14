#ifndef ABCD_SOURCE_DESCENDANT_CLASSES_H_
#define ABCD_SOURCE_DESCENDANT_CLASSES_H_
#include "creator_classes.h"
namespace abcd::source {
// Occurrence-indexed preparation, owned until the corresponding creator is
// built. Class layouts are shared only with that creator's draft constants.
struct DescendantClasses {
  std::vector<std::optional<CreatorClasses>> creators;
  std::vector<NameReference> references;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  std::uint64_t owner_end = 0;
};
[[nodiscard]] std::variant<DescendantClasses, FamilyTreeFailure> PrepareDescendantClasses(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const FileFamily&, bool entry, const GlobalScope*,
    RecoveryMode, analysis::SemanticBudget = {});
[[nodiscard]] std::variant<CreatorClassCost, FamilyTreeFailure> FinishDescendantClasses(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const FileFamily&, FamilyId, CreatorClasses&,
    const GlobalScope*, const CaptureTree&, RecoveryMode, analysis::SemanticBudget = {});
}
#endif
