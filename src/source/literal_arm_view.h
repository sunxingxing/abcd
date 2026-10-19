#ifndef ABCD_SOURCE_LITERAL_ARM_VIEW_H_
#define ABCD_SOURCE_LITERAL_ARM_VIEW_H_
#include "../analysis/value_flow.h"
#include "evaluation_plan.h"
#include "literal_event_view.h"
#include "phase_corridor.h"
namespace abcd::source {
enum class LiteralArmError : std::uint8_t {
  kCoverage, kEscape, kOrder, kCatchState, kCorrespondence
};
struct LiteralArmView {
  analysis::OpId allocation;
  analysis::ValueId value;
  analysis::EdgeId entry, exit;
  analysis::UseId phi;
  std::size_t events;
  bool operator==(const LiteralArmView&) const = default;
};
using LiteralArmResult = std::variant<LiteralArmView, LiteralArmError>;
// Shared immutable-graph indexes. Requires verified LiteralPlan and SSA.
// Phased queries retain Commit availability and the common catch/no-staging
// obligations. Self-referencing phase scratch makes the index non-movable. Arm
// queries are cached by the allocation's unique phi occurrence. Successful
// dominated ranges are disjoint: each contains exactly one entire forest and
// no foreign operation. No per-construction whole-CFG traversal.
class LiteralArmIndex {
 public:
  LiteralArmIndex(const analysis::ValueProgram&, const EvaluationPlan&, TreeCheckBudget&);
  LiteralArmIndex(const LiteralArmIndex&) = delete;
  LiteralArmIndex& operator=(const LiteralArmIndex&) = delete;
  LiteralArmIndex(LiteralArmIndex&&) = delete;
  LiteralArmIndex& operator=(LiteralArmIndex&&) = delete;
  LiteralArmResult Prove(analysis::EdgeId entry, analysis::UseId phi);
  void Verify(const LiteralArmView&);
 private:
  const analysis::ValueProgram& graph_;
  const EvaluationPlan& plan_;
  TreeCheckBudget& budget_;
  LiteralEventView view_;
  analysis::DominatorTree dominators_;
  std::vector<std::size_t> counts_, prefix_, first_, cursor_;
  std::vector<analysis::OpId> members_;
  std::vector<analysis::WorkBlockId> blocks_, queue_;
  std::vector<std::uint32_t> incoming_, indegree_;
  std::vector<std::optional<analysis::UseId>> external_;
  std::vector<std::uint8_t> escaped_;
  std::vector<std::optional<LiteralArmResult>> cached_;
  std::optional<PhaseBudget> phase_budget_;
  std::optional<PhaseCorridors> phases_;
  std::uint64_t accounted_work_ = 0;
  LiteralArmResult Derive(analysis::OpId, analysis::EdgeId, analysis::UseId);
};
// Only unmet member references seed this extension: a selected phi is stored
// by a private outer literal whose sole external observation is that argument.
// Returns certified original phi occurrences; no names or placements allocated.
[[nodiscard]] std::vector<std::optional<analysis::UseId>>
IndexNestedLiteralInputs(const analysis::ValueProgram&, const EvaluationPlan&,
                         TreeCheckBudget&);
}  // namespace abcd::source
#endif
