#ifndef ABCD_SOURCE_EXPRESSION_COPIES_H_
#define ABCD_SOURCE_EXPRESSION_COPIES_H_

#include "expression_forest.h"

namespace abcd::source {
// Internal construction step after verified evaluation/binding and capacity
// admission. Appends exclusively owned copy expressions and synthetic origins;
// never expands an operation subtree. No allocations beyond admitted vectors.
// Failure leaves an unpublished partial forest, which the caller must discard.
[[nodiscard]] std::variant<std::uint64_t, ExpressionError> AppendCopyExpressions(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    ExpressionForest& forest, FunctionOwnerId owner, std::uint64_t work);
}  // namespace abcd::source
#endif
