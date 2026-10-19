#ifndef ABCD_SOURCE_LITERAL_EVENT_VIEW_H_
#define ABCD_SOURCE_LITERAL_EVENT_VIEW_H_

#include "literal_plan.h"
#include "tree_checks.h"

namespace abcd::source {
// A census, not relocation permission. Selection composition must additionally
// prove conditional coverage, single consumption, order and catch agreement.
// All identities refer to the original graph/verified LiteralPlan; no op is
// replaced by the allocation and no exception exit is erased.
enum class LiteralEventRole : std::uint8_t {
  kAllocation, kDefinition, kOperand
};
struct LiteralEvent {
  analysis::OpId operation;
  analysis::OpId allocation;
  LiteralEventRole role;
  std::optional<analysis::UseId> consumer;
  std::optional<analysis::WorkBlockId> commit;
  std::optional<std::size_t> exception_exit;
  bool operator==(const LiteralEvent&) const = default;
};
struct LiteralEventView {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  // OpId-indexed; absent means ordinary evaluation retains ownership.
  std::vector<std::optional<LiteralEvent>> events;
};
enum class LiteralEventError : std::uint8_t {
  kRevision, kCoverage, kOwnership, kCorrespondence
};
// Requires the existing LiteralPlan verifier to have succeeded against graph.
// Does not inspect mutable evaluation placements or invent ownership from them.
// O(ops + definitions + nested operands + exception exits), prepaid storage.
[[nodiscard]] LiteralEventView BuildLiteralEventView(
    const analysis::ValueProgram&, const LiteralPlan&, TreeCheckBudget&);
// Recomputes from immutable original identities and the independently verified
// LiteralPlan. The supplied event set, roles, consumers, commits and exits are
// never used to derive expected ownership. Throws LiteralEventError on mismatch.
void VerifyLiteralEventView(const analysis::ValueProgram&, const LiteralPlan&,
    const LiteralEventView&, TreeCheckBudget&);
}  // namespace abcd::source
#endif
