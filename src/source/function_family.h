#ifndef ABCD_SOURCE_FUNCTION_FAMILY_H_
#define ABCD_SOURCE_FUNCTION_FAMILY_H_

#include "capture_tree.h"

namespace abcd::source {
struct FamilyId { std::uint32_t value; auto operator<=>(const FamilyId&) const = default; };
struct FamilyMember {
  FunctionTree tree;
  TreeFlow flow;
  // Verified input/work proof for this member's incoming creation; absent for
  // the root. The graph/input products used to check it must remain unchanged.
  std::optional<CaptureCells> incoming;
};
struct FamilyParent { FamilyId function; ExprId expression; };
struct ConstructedFamily {
  std::vector<FamilyMember> members;
  FamilyId root;
  std::vector<std::optional<FamilyParent>> parents;
  std::vector<FamilyId> order; // Parent before child, independent of input order.
  std::size_t allocated_bytes = 0; // Additional retained layout, not input members.
  std::uint64_t work = 0;
};
enum class FamilyError : std::uint8_t {
  kRoot, kOwner, kMissingChild, kMultipleParents, kCycle, kCapture,
  kResourceLimit, kWorkLimit
};
// Takes ownership of unchanged common tree/flow and checked capture products.
// Caller admits their full storage (including moved vector capacity) alongside
// this call's additional layout/scratch allowance. No per-edge graph replay.
// A conditional construction product, NOT immutable publication evidence:
// callable/name/strictness, root enclosure and transitive effects remain gates.
[[nodiscard]] std::variant<ConstructedFamily, FamilyError> BuildFunctionFamily(
    std::vector<FamilyMember> members, FunctionOwnerId root,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
