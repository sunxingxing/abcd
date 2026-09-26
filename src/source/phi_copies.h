#ifndef ABCD_SOURCE_PHI_COPIES_H_
#define ABCD_SOURCE_PHI_COPIES_H_

#include "edge_copies.h"

namespace abcd::source {
struct PhiBundle {
  analysis::IncomingKey incoming;
  std::uint32_t first;
  std::uint32_t count;
  EdgeCopies schedule;
};
struct PhiCopies {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ParallelCopy> copies;
  std::vector<analysis::UseId> inputs;
  std::vector<PhiBundle> bundles;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class PhiCopyError : std::uint8_t {
  kRevision, kFlow, kDomain, kInput, kBundle, kSchedule, kResourceLimit, kWorkLimit
};
// Keeps MethodEntry separate from CFG edges, including entry-header backedges.
// Fresh normal-flow verification proves incoming availability, not source
// placement or default/cell initialization order. No CFG/source mutation here.
[[nodiscard]] std::variant<PhiCopies, PhiCopyError> BuildPhiCopies(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {});
// Checks complete occurrence coverage and supplied edge bundles/schedules, not
// a regenerated plan. Source publication additionally needs EdgePlacement and
// provisional Symbol ownership; a successful report alone cannot admit phis.
[[nodiscard]] std::variant<std::uint64_t, PhiCopyError> VerifyPhiCopies(
    const analysis::ValueProgram& graph, const PhiCopies& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
