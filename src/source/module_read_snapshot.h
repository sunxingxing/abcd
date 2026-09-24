#ifndef ABCD_SOURCE_MODULE_READ_SNAPSHOT_H_
#define ABCD_SOURCE_MODULE_READ_SNAPSHOT_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
struct ModuleReadSnapshot {
  std::vector<analysis::LanguageLoadFact> loads;
  std::uint64_t work_units = 0;
  std::size_t AllocatedBytes() const { return loads.capacity() * sizeof(analysis::LanguageLoadFact); }
};
// Same-record immutable callable-creation context. Only already-proven local
// export stores justify initialization; imports/names/metadata alone do not.
// O(I) scan, O(M) retained storage for I instructions and M qualifying reads.
std::variant<ModuleReadSnapshot, SourceFailure> SnapshotModuleReads(
    std::span<const analysis::SemanticInstruction> instructions,
    const MethodConstants& constants, analysis::SemanticBudget budget);
}  // namespace abcd::source
#endif
