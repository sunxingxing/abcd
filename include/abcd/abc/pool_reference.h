#ifndef ABCD_ABC_POOL_REFERENCE_H_
#define ABCD_ABC_POOL_REFERENCE_H_

#include "abcd/abc/metadata_index.h"

namespace abcd::abc {

struct PoolIndex {
  std::uint32_t value = 0;
  auto operator<=>(const PoolIndex&) const = default;
};

enum class PoolEntityKind { kMethod, kString, kLiteralArray };

struct PoolReference {
  PoolEntityKind kind;
  EntityOffset offset;
};

// Requires an unmodified index built from this file/header. No target payload
// is read: kind is the consuming operand's expectation, not a certification
// that the referenced bytes encode that entity. O(log R), bounded stack space.
[[nodiscard]] std::variant<PoolReference, MetadataError> ResolvePoolReference(
    const corpus::AbcFile& file, const Header& header,
    const MetadataIndex& index, EntityOffset enclosing_method, PoolIndex slot,
    PoolEntityKind kind);

}  // namespace abcd::abc

#endif  // ABCD_ABC_POOL_REFERENCE_H_
