#ifndef ABCD_SOURCE_COMPLETION_TREE_H_
#define ABCD_SOURCE_COMPLETION_TREE_H_

#include "completion_plan.h"
#include "tree_checks.h"
#include "tree_flow.h"

namespace abcd::source {
// Reuse checked operation placements, preserving both cleanup occurrences and
// their original SSA identities. No syntax or executable strings are inferred
// by the printer. Unsupported placements retain their expanded source tree.
// Rule: move the already-fixed return/rethrow into the protected body and put
// its original cleanup occurrence in finally. A private per-entry boolean picks
// the exceptional copy. Normal cleanup preserves the pending completion; its
// throw replaces it. No cleanup is moved inside the original catch protection.
// This first projection intentionally does not deduplicate the two SSA trees.
void RecoverFinalizers(const CompletionPlan& plan, const TreeFlow& flow,
                       FunctionTree& tree, TreeCheckBudget& budget);

// A sequence projection only: borrows expressions, symbols and statements from
// the real AST. The actual AST is checked first for ownership and lexical
// scope. Restoring the original corridor order lets the existing edge/operand
// checks account for every original operation without storing a second source
// tree.
class CompletionTreeView {
 public:
  CompletionTreeView(const FunctionTree& tree, const CompletionPlan& plan,
                     TreeCheckBudget& budget);
  const BlockStmt& Block(StmtId statement) const;
  StmtId Child(std::uint32_t index) const;
  const AnchorPosition& Anchor(AnchorId anchor) const;
  ScopeId TryParent(StmtId statement, ScopeId parent) const;
  void Finish(TreeFlow& flow, TreeCheckBudget& budget) const;
  const std::vector<SymbolId>& Symbols() const { return symbols_; }

 private:
  struct Corridor {
    const FinalizerRegion* plan;
    StmtId normal, exceptional;
  };
  const FunctionTree& tree_;
  std::vector<BlockStmt> blocks_;
  std::vector<StmtId> children_;
  std::vector<AnchorPosition> anchors_;
  std::vector<std::optional<ScopeId>> parents_;
  std::vector<SymbolId> symbols_;
  std::vector<ExprId> expressions_;
  std::vector<Corridor> corridors_;
};
}  // namespace abcd::source
#endif
