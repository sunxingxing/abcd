#ifndef ABCD_ABC_CALL_TYPE_H_
#define ABCD_ABC_CALL_TYPE_H_

#include "abcd/abc/metadata_index.h"

namespace abcd::abc {
struct CallType {
  // Absence means no matching element, not a failed read. Pass only a
  // successful value to DecodeCallFlags; retain provenance for diagnostics.
  std::optional<std::uint32_t> value;
};

// Requires sorted validated index regions and the same immutable file used
// to build MethodAnnotations. Reads payloads lazily, preserving encoded order.
// The budget bounds transient string heap storage; fixed cursor stack storage
// must also be covered by the caller's process-wide worker admission budget.
// Historical profiles share container decoding, not proven runtime semantics.
[[nodiscard]] std::variant<CallType, MetadataError> ReadCallType(
    const corpus::AbcFile& file, const Header& header,
    const MetadataIndex& index, MethodAnnotations annotations,
    MetadataBudget budget = {});
}  // namespace abcd::abc
#endif  // ABCD_ABC_CALL_TYPE_H_
