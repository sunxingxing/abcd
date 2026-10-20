#ifndef ABCD_SOURCE_SWITCH_REGION_H_
#define ABCD_SOURCE_SWITCH_REGION_H_
#include "control_skeleton.h"
namespace abcd::source {
struct SwitchTest {
  analysis::WorkBlockId block;
  analysis::OpId branch, comparison;
  analysis::ValueId discriminant;
  analysis::EdgeId match, miss;
  bool discriminant_on_left = true;
  bool operator==(const SwitchTest&) const = default;
};
struct SwitchClause {
  std::optional<SwitchTest> test;
  // Empty adjacent labels do not claim their shared body's entry twice.
  std::optional<analysis::WorkBlockId> body;
  bool operator==(const SwitchClause&) const = default;
};
struct SwitchRegion {
  analysis::WorkBlockId entry;
  std::optional<analysis::WorkBlockId> follow;
  analysis::ValueId discriminant;
  std::uint32_t first_clause, clause_count;
  bool operator==(const SwitchRegion&) const = default;
};
enum class SwitchPortKind : std::uint8_t {
  kMatch,
  kMiss,
  kBreak,
  kFallThrough
};
struct SwitchPortEvidence {
  std::uint32_t region, clause;
  analysis::EdgeId edge;
  SwitchPortKind kind;
  bool operator==(const SwitchPortEvidence&) const = default;
};
struct SwitchBlockOwner {
  std::uint32_t region;
  // Absence denotes test/discriminant evaluation, not a case body.
  std::optional<std::uint32_t> clause;
  bool operator==(const SwitchBlockOwner&) const = default;
};
struct SwitchRegions {
  ControlFailure seed;
  std::vector<SwitchRegion> regions;
  std::vector<SwitchClause> clauses;
  std::vector<SwitchPortEvidence> ports;
  std::vector<std::optional<SwitchBlockOwner>> blocks;
  std::vector<ScopedControlEntry> entries;
  std::size_t allocated_bytes = 0;
};
[[nodiscard]] ControlPlan BuildSwitchControl(const analysis::ValueProgram&,
                                             const ControlFailure&,
                                             ControlQuota&);
void VerifySwitchControl(const analysis::ValueProgram&, const ControlPlan&,
                         ControlQuota&);
}  // namespace abcd::source
#endif
