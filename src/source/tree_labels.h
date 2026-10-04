#ifndef ABCD_SOURCE_TREE_LABELS_H_
#define ABCD_SOURCE_TREE_LABELS_H_

#include "tree_structure.h"

namespace abcd::source {
// Source construction pass: label block exits and non-nearest loop transfers.
// Iterative O(statements + child occurrences), including deeply nested loops. Never crosses
// FunctionExpr into another FunctionTree. Malformed ownership/targets are errors.
[[nodiscard]] std::variant<TreeStorage, TreeStructureError> AssignTreeLabels(
    FunctionTree& tree, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
