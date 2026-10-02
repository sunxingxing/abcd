#ifndef ABCD_SOURCE_FUNCTION_FAMILY_H_
#define ABCD_SOURCE_FUNCTION_FAMILY_H_

#include "capture_tree.h"
#include "abcd/source/function_abi.h"
#include "instruction_sources.h"

namespace abcd::source {
struct FamilyId { std::uint32_t value; auto operator<=>(const FamilyId&) const = default; };
struct FamilyMember {
  FunctionTree tree;
  TreeFlow flow;
  // Verified input/work proof for this member's incoming creation; absent for
  // the root. The graph/input products used to check it must remain unchanged.
  std::optional<CaptureCells> incoming;
  // Same-input census retained by file-backed construction. Synthetic layout
  // fixtures may omit it; a mutable census is not publication evidence.
  std::optional<FunctionAbi> abi;
  std::optional<InstructionSources> sources;
  // File-backed factory provenance. Synthetic layout inputs may omit it, but
  // module publication must compare it with the actual destination record.
  std::optional<abc::EntityOffset> record;
};
struct FamilyParent { FamilyId function; ExprId expression; };
// Construction diagnostics only, not transferable publication evidence.
struct FamilyWriteFacts {
  std::size_t stores = 0;
  std::size_t captured_stores = 0;
  std::size_t signature_stores = 0;
  std::size_t creations = 0;
  std::size_t primitive_objects = 0;
  std::size_t primitive_arrays = 0;
};
struct ConstructedFamily {
  std::vector<FamilyMember> members;
  FamilyId root;
  std::vector<std::optional<FamilyParent>> parents;
  std::vector<FamilyId> order; // Parent before child, independent of input order.
  std::size_t allocated_bytes = 0; // Additional retained layout, not input members.
  std::uint64_t work = 0;
  std::optional<FamilyWriteFacts> writes;
  // Owned external class-root cell evidence, separate from intra-family edges.
  // The actual enclosing syntax is borrowed anew at each embedding.
  std::optional<CaptureCells> root_capture;
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
// A marked partial child can retain a checked capture-free creation without
// complete child flow. Its body remains incomplete; captured gaps are rejected.
[[nodiscard]] std::variant<ConstructedFamily, FamilyError> BuildFunctionFamily(
    std::vector<FamilyMember> members, FunctionOwnerId root,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
