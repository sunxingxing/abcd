#ifndef ABCD_SOURCE_TREE_PRINT_FAILURE_H_
#define ABCD_SOURCE_TREE_PRINT_FAILURE_H_
#include "abcd/source/tree_failure.h"
#include "abcd/source/concurrent_failure.h"
#include <limits>
namespace abcd::source {
enum class TreePrintError : std::uint8_t {
  kUnsupportedContext, kOutputLimit, kWrite, kResourceLimit, kWorkLimit, kInvalidLiteral
};
enum class TreeStructureError : std::uint8_t {
  kRange, kOwnership, kScope, kBinding, kName, kTarget, kOrigin,
  kUnsupportedSyntax, kResourceLimit, kWorkLimit, kGlobalShadowed,
  kRegExpSyntax, kRegExpUnproven, kRegExpSource
};
enum class TreePrintBudgetKind : std::uint8_t {
  kScratchBytes, kOutputBytes, kWork, kCapacityOverflow, kHostAllocation
};
enum class TreePrintSite : std::uint8_t {
  kStructureVerification, kVariableProjection, kClassNamePlanning,
  kPositionTable, kActionStack, kClassMemberRetention, kTokenOutput,
  kDestinationVerification, kTreeStorage, kPublication
};
// A failed charge reports its own ledger, not the caller's remaining budget.
// Missing requested is intentional for host allocation / arithmetic overflow.
struct TreeBudgetEvidence {
  TreePrintBudgetKind budget_kind = TreePrintBudgetKind::kScratchBytes;
  std::optional<std::uint64_t> requested;
  std::uint64_t remaining=0, limit=0, used=0, peak=0, retained=0;
  bool operator==(const TreeBudgetEvidence&) const = default;
};
struct TreePrintFailure {
  TreePrintError kind;
  std::optional<TreeBudgetEvidence> budget;
  std::variant<std::monostate,TreeStructureError> cause;
  TreePrintSite site = TreePrintSite::kPublication;
  std::size_t written_bytes=0;
  TreeLocation location;
  // Owner is the family's stable FunctionOwnerId (not a loop index).
  std::optional<FunctionOwnerId> owner;
  std::optional<AnchorId> anchor;
  std::optional<ConcurrentFailure> concurrent;
  TreePrintFailure(TreePrintError value) : kind(value) {}
  bool operator==(TreePrintError value) const { return kind==value; }
  explicit operator unsigned() const { return static_cast<unsigned>(kind); }
};
}
#endif
