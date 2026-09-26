#ifndef ABCD_SOURCE_TREE_EDGE_FLOW_H_
#define ABCD_SOURCE_TREE_EDGE_FLOW_H_

#include "tree_checks.h"
#include "tree_flow.h"

namespace abcd::source {
// Internal to fresh TreeFlow verification. Consume is called only at the
// control check's exact edge boundary, never at a guessed target-block entry.
class TreeEdgeFlow {
 public:
  TreeEdgeFlow(const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
      const FunctionTree& tree, TreeFlow& flow, std::vector<std::uint8_t>& declared,
      std::vector<std::uint8_t>& assigned, TreeCheckBudget& budget);
  void Consume(analysis::EdgeId edge, const BlockStmt& block, std::uint32_t& child);
  void Finish();
 private:
  void Bind(std::optional<SymbolId>& binding, SymbolId symbol, TemporaryReason reason);
  const analysis::ValueProgram& graph_;
  const EvaluationPlan& evaluation_;
  const FunctionTree& tree_;
  TreeFlow& flow_;
  std::vector<std::uint8_t>& declared_;
  std::vector<std::uint8_t>& assigned_;
  TreeCheckBudget& budget_;
  std::vector<std::optional<std::size_t>> bundles_;
  std::vector<std::uint8_t> visited_;
};
}  // namespace abcd::source
#endif
