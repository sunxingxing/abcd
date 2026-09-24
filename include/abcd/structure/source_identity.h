#ifndef ABCD_STRUCTURE_SOURCE_IDENTITY_H_
#define ABCD_STRUCTURE_SOURCE_IDENTITY_H_

#include <string>
#include <variant>

#include "abcd/structure/record_writer.h"

namespace abcd::structure {
enum class SourceIdentityState { kMissing, kUnique, kConflicting, kIncomplete };
struct SourceIdentity {
  SourceIdentityState state = SourceIdentityState::kMissing;
  // Uninterpreted ABC filename, populated only for unique evidence. This is
  // textual data, never host-path authority or proof of the original language.
  std::u16string file;
};
using SourceIdentityResult = std::variant<SourceIdentity, StructureFailure>;

// Reads class/method filename evidence only, never embedded source code,
// sidecars or source maps. Method payloads are decoded one at a time within
// their quotas; the caller admits the retained unique name in its own budget.
[[nodiscard]] SourceIdentityResult ReadSourceIdentity(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::RecordInfo& record,
    StructureBudget budget = {});
}  // namespace abcd::structure
#endif  // ABCD_STRUCTURE_SOURCE_IDENTITY_H_
