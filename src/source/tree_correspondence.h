#ifndef ABCD_SOURCE_TREE_CORRESPONDENCE_H_
#define ABCD_SOURCE_TREE_CORRESPONDENCE_H_

#include "tree_checks.h"
#include "tree_semantics.h"

namespace abcd::source {
inline void RequireMeaning(bool condition, TreeSemanticError error) {
  if (!condition) throw error;
}
// Internal stages consume the fresh structural/control checks from the same
// invocation. Never accept cached mutable TreeFlow as a public certificate.
void CheckTreeOperands(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    const TreeFlow& flow, ExpressionConstants constants, TreeCheckBudget& budget);
void CheckTreeCorrespondenceOrigins(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const FunctionTree& tree, const TreeFlow& flow, TreeCheckBudget& budget);
}  // namespace abcd::source
#endif
