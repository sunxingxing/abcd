#ifndef ABCD_SOURCE_PHI_COPIES_H_
#define ABCD_SOURCE_PHI_COPIES_H_

#include "edge_copies.h"
#include "for_of_evaluation_plan.h"

namespace abcd::source {
struct LiteralPlan;
struct EvaluationPlan;
struct ForInPlan;
class TreeCheckBudget;
enum class PhiFlow : std::uint8_t { kNormal, kPhased };
enum class PhiDomain : std::uint8_t { kLanguage, kHoleTransport };
struct PhiBundle {
  analysis::IncomingKey incoming;
  std::uint32_t first;
  std::uint32_t count;
  EdgeCopies schedule;
  std::optional<analysis::UseId> expression_input;
};
enum class PhiCopyUse : std::uint8_t { kRetained, kIdentity, kForInProtocol, kForOfProtocol };
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
  // Original occurrences remain complete; only proved normal phi identities
  // disappear from executable schedules. Exceptional staging stays intact.
  std::vector<PhiCopyUse> projection;
  // Original phi occurrences consumed by the independently verified for-in
  // identity graph. They have no source parallel-copy destination or symbol.
  std::vector<analysis::UseId> for_in_inputs;
  std::vector<analysis::UseId> for_of_inputs;

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
// Fresh PhiCopies, checked module/literal indexes and optional verified flat
// alias identities are prerequisites. Alias transport keeps the original UseId.
// Sole observations move to their original phi input only when all normal
// paths from completion reach that edge once through inert evaluation, without
// another copy, effect, environment change or divergence. No producer whitelist
// or Commit adjacency. Original pre-throw staging remains at its original site;
// AST flow/operand/scope checks still own the eventual source placement.
// With a freshly verified normal source plan, only an unmet member argument
// may additionally consume a certified cell guard with its read. Guard
// dominance, a unique pair, caller exception correspondence and the same
// all-path inert tail are rederived; no binding/TDZ premise is added. Existing
// eligible inputs are unchanged. See guarded-call-references.md.
// Shared censuses/tails plus one dominance proof: O((V+E) log V) at most.
[[nodiscard]] std::vector<std::optional<analysis::UseId>> IndexPhiExpressionInputs(
    const analysis::ValueProgram& graph, const PhiCopies& copies,
    std::span<const std::optional<analysis::OpId>> module_guards,
    std::span<const std::optional<analysis::OpId>> literal_ends, TreeCheckBudget& budget,
    std::span<const analysis::ValueId> aliases = {},
    const EvaluationPlan* source = nullptr);
// O(ops + definitions), no per-phi literal inventory scan. Statements/corridors
// are excluded: only verified same-block native literal expressions qualify.
[[nodiscard]] std::vector<std::optional<analysis::OpId>> IndexPhiLiteralEnds(
    const analysis::ValueProgram& graph, const LiteralPlan& literals, TreeCheckBudget& budget);
// Same parallel-copy algorithm, but availability is Before/Commit/Catch aware.
// Abstract edge schedules are NOT permission to execute writes before a throw.
// Exceptional edges require separate staging and catch-entry commit placement.
[[nodiscard]] std::variant<PhiCopies, PhiCopyError> BuildPhasedPhiCopies(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {},
    PhiDomain domain = PhiDomain::kLanguage, const ForInPlan* for_in = nullptr, std::span<const ForOfUseDisposition> for_of = {});
[[nodiscard]] std::variant<std::uint64_t, PhiCopyError> VerifyPhasedPhiCopies(
    const analysis::ValueProgram& graph, const PhiCopies& plan,
    analysis::SemanticBudget budget = {}, std::span<const ForOfUseDisposition> for_of = {});
}  // namespace abcd::source
#endif
