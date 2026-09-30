#ifndef ABCD_SOURCE_TREE_EDGE_FLOW_H_
#define ABCD_SOURCE_TREE_EDGE_FLOW_H_

#include "tree_checks.h"
#include "tree_flow.h"

namespace abcd::source {
class CompletionTreeView;
// Internal to fresh TreeFlow verification. Consume is called only at the
// control check's exact edge boundary, never at a guessed target-block entry.
class TreeEdgeFlow {
 public:
  TreeEdgeFlow(const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
      const FunctionTree& tree, TreeFlow& flow, std::vector<std::uint8_t>& declared,
      std::vector<std::uint8_t>& assigned, TreeCheckBudget& budget,
      const CompletionTreeView& sequences);
  void Consume(analysis::IncomingKey incoming, const BlockStmt& block, std::uint32_t& child);
  void Stage(analysis::OpId operation, const BlockStmt& block, std::uint32_t& child);
  void Catch(analysis::WorkBlockId handler, const BlockStmt& block, std::uint32_t& child);
  [[nodiscard]] bool HasCopies(analysis::EdgeId edge) const;
  void Finish();
 private:
  void Bind(std::optional<SymbolId>& binding, SymbolId symbol, TemporaryReason reason);
  ExprId Assignment(analysis::IncomingKey incoming, const BlockStmt& block, std::uint32_t& child,
      bool allow_phi_expression = false);
  const analysis::ValueProgram& graph_;
  const EvaluationPlan& evaluation_;
  const FunctionTree& tree_;
  const CompletionTreeView& sequences_;
  TreeFlow& flow_;
  std::vector<std::uint8_t>& declared_;
  std::vector<std::uint8_t>& assigned_;
  TreeCheckBudget& budget_;
  std::vector<std::optional<std::size_t>> bundles_;
  std::vector<std::uint8_t> visited_;
  std::optional<std::size_t> entry_;
  std::vector<std::optional<std::size_t>> throws_, catches_;
  std::vector<std::uint8_t> committed_;
};
}  // namespace abcd::source
#endif
