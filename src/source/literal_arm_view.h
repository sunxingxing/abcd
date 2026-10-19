#ifndef ABCD_SOURCE_LITERAL_ARM_VIEW_H_
#define ABCD_SOURCE_LITERAL_ARM_VIEW_H_
#include "literal_event_view.h"
#include "evaluation_plan.h"
#include "../analysis/value_flow.h"
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
// Shared immutable-graph indexes. Requires verified LiteralPlan and SSA. Arm
// queries are cached by the allocation's unique phi occurrence. Successful
// dominated ranges are disjoint: each contains exactly one entire forest and
// no foreign operation. No per-construction whole-CFG traversal.
class LiteralArmIndex {
 public:
  LiteralArmIndex(const analysis::ValueProgram&, const EvaluationPlan&, TreeCheckBudget&);
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
  LiteralArmResult Derive(analysis::OpId, analysis::EdgeId, analysis::UseId);
};
}  // namespace abcd::source
#endif
