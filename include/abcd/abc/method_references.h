#ifndef ABCD_ABC_METHOD_REFERENCES_H_
#define ABCD_ABC_METHOD_REFERENCES_H_

#include "abcd/abc/method_body.h"
#include "abcd/abc/pool_reference.h"

namespace abcd::abc {

struct MethodReference {
  PoolIndex slot;
  PoolReference target;
  CodeOffset first_use;
};
struct MethodReferences {
  // Sorted by target kind, then slot. Distinct roles sharing a slot remain
  // distinct; repeated uses of the same role/slot resolve only once.
  std::vector<MethodReference> references;
  std::uint64_t operand_count = 0;
};
enum class ReferenceError {
  kMalformedOperands,
  kUnsupportedOperand,
  kIndexOverflow,
  kResolution,
  kResourceLimit,
};
struct ReferenceFailure {
  ReferenceError kind;
  CodeOffset pc;
  std::optional<MetadataError> metadata_error;
};
struct ReferenceBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};
using ReferenceResult = std::variant<MethodReferences, ReferenceFailure>;

// Requires metadata and decoded body from the same immutable file/header.
// Resolves ranges only, not target payloads, historical semantics or symbol
// bindings. O(I + K log K + U log R), O(K) bounded capacity for I instructions,
// K reference operands, U unique role/slot pairs and R regions.
[[nodiscard]] ReferenceResult ResolveMethodReferences(
    const corpus::AbcFile& file, const Header& header,
    const MetadataIndex& index, EntityOffset method, const MethodBody& body,
    ReferenceBudget budget = {});

}  // namespace abcd::abc
#endif  // ABCD_ABC_METHOD_REFERENCES_H_
