#ifndef ABCD_SOURCE_FOR_IN_EVENTS_H_
#define ABCD_SOURCE_FOR_IN_EVENTS_H_
#include "for_in_plan.h"
namespace abcd::source {
// An instruction's normal Commit is distinct from the successful done edge.
// In particular next commits Undefined on exhaustion without writing a key.
struct ForInExceptionExitId {
  std::uint32_t value;
  bool operator==(const ForInExceptionExitId&) const = default;
};
struct ForInEvent {
  analysis::OpId operation;
  analysis::WorkBlockId before, commit;
  analysis::ValueId result;
  ForInExceptionExitId exceptional_exit;
  bool operator==(const ForInEvent&) const = default;
};
struct ForInKeyCommit {
  analysis::OpId decision;
  analysis::EdgeId success, exhausted;
  analysis::ValueId next_result;
  bool operator==(const ForInKeyCommit&) const = default;
};
struct ForInEventPair {
  ForInEvent acquisition, advance;
  ForInKeyCommit key;
  bool operator==(const ForInEventPair&) const = default;
};
struct ForInEvents {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ForInEventPair> loops; // Same acquisition order as ForInPlan.
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
[[nodiscard]] std::variant<ForInEvents, ForInFailure> BuildForInEvents(
    const analysis::ValueProgram&, const ForInPlan&, analysis::SemanticBudget = {});
// Input must satisfy the same verified-input prerequisites as ForInPlan.
// This is protocol evidence only: it does not authorize a source binding,
// handler removal or tree admission. A tree adapter must separately bind the
// success-only key event and retain all raw sites and exit correspondences.
// Rechecks immutable phase identities, result definitions, exception exits and
// original successor edges; never equates a next result with its key commit.
[[nodiscard]] std::variant<std::uint64_t, ForInFailure> VerifyForInEvents(
    const analysis::ValueProgram&, const ForInPlan&, const ForInEvents&,
    analysis::SemanticBudget = {});
} // namespace abcd::source
#endif
