#ifndef ABCD_SOURCE_PHI_COPIES_H_
#define ABCD_SOURCE_PHI_COPIES_H_

#include "edge_copies.h"

namespace abcd::source {
struct LiteralPlan;
class TreeCheckBudget;
enum class PhiFlow : std::uint8_t { kNormal, kPhased };
enum class PhiDomain : std::uint8_t { kLanguage, kHoleTransport };
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
  PhiFlow flow = PhiFlow::kNormal;
  PhiDomain domain = PhiDomain::kLanguage;
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
// A sole-use expression at the end of a single-successor normal block may be
// evaluated directly by its single phi assignment. Requires verified PhiCopies;
// module_guards, when supplied, is the checked symmetric module-pair index.
// Its read owns the original guard use/event, not an additional source use.
// literal_ends is the index of freshly checked native expression literals;
// their unobserved initialization interval remains inside the same edge RHS.
// Phased inputs admit only nonthrowing primitive literals, through single-exit
// inert tails after their Commit; catch edges and computed values stay pinned.
// Tail scans stop at any other producer, so eligible suffixes are disjoint and
// visiting all bundles is linear in graph size.
[[nodiscard]] std::optional<analysis::OpId> PhiExpressionProducer(
    const analysis::ValueProgram& graph, const PhiCopies& copies, const PhiBundle& bundle,
    std::span<const std::optional<analysis::OpId>> module_guards = {},
    std::span<const std::optional<analysis::OpId>> literal_ends = {});
// O(ops + definitions), no per-phi literal inventory scan. Statements/corridors
// are excluded: only verified same-block native literal expressions qualify.
[[nodiscard]] std::vector<std::optional<analysis::OpId>> IndexPhiLiteralEnds(
    const analysis::ValueProgram& graph, const LiteralPlan& literals, TreeCheckBudget& budget);
// Same parallel-copy algorithm, but availability is Before/Commit/Catch aware.
// Abstract edge schedules are NOT permission to execute writes before a throw.
// Exceptional edges require separate staging and catch-entry commit placement.
[[nodiscard]] std::variant<PhiCopies, PhiCopyError> BuildPhasedPhiCopies(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {},
    PhiDomain domain = PhiDomain::kLanguage);
[[nodiscard]] std::variant<std::uint64_t, PhiCopyError> VerifyPhasedPhiCopies(
    const analysis::ValueProgram& graph, const PhiCopies& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
