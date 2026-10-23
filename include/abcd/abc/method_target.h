#ifndef ABCD_ABC_METHOD_TARGET_H_
#define ABCD_ABC_METHOD_TARGET_H_

#include "abcd/abc/method_attributes.h"
#include "abcd/abc/method_lookup.h"
#include "abcd/abc/pool_reference.h"

namespace abcd::abc {
struct MethodTarget {
  MethodPosition position;
  MethodAttributes attributes;
};
enum class MethodTargetError { kCallerMissing, kTargetMissing, kIndexMismatch };
using MethodTargetResult = std::variant<MethodTarget, MethodTargetError,
                                        MetadataError, MethodAttributeError>;

// All arguments must share an unchanged file/index context. Resolve in the
// creating caller's region, then require actual local method membership.
// A missing foreign target is explicit, not an invented local body. This does
// not establish JS function syntax, capture mode or runtime object identity.
// O(log R + log M), constant auxiliary space; lookup is built once per file.
[[nodiscard]] MethodTargetResult ResolveMethodTarget(
    const corpus::AbcFile& file, const Header& header,
    const MetadataIndex& index, const MethodLookup& lookup, EntityOffset caller,
    PoolIndex slot);
// Literal-buffer METHOD entries name a method entity directly, not a pool
// slot. Same caller/target membership and attribute decoding as above.
[[nodiscard]] MethodTargetResult ResolveLiteralMethodTarget(
    const MetadataIndex& index, const Header& header, const MethodLookup& lookup,
    EntityOffset caller, EntityOffset target);
}  // namespace abcd::abc
#endif  // ABCD_ABC_METHOD_TARGET_H_
