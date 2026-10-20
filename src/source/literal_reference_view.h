#ifndef ABCD_SOURCE_LITERAL_REFERENCE_VIEW_H_
#define ABCD_SOURCE_LITERAL_REFERENCE_VIEW_H_
#include "evaluation_plan.h"
#include "tree_checks.h"
namespace abcd::source {
// A verified staged literal remains an ordered, throwing forest. This view is
// a separate composition boundary; ordinary allocation exclusion still holds.
struct LiteralReferenceView {
  analysis::OpId allocation, last, call, callee;
  analysis::UseId argument;
  std::size_t events = 0;
  bool may_throw = false;
  std::optional<analysis::OpId> thrower;
  bool operator==(const LiteralReferenceView&) const = default;
};
// Requires independent LiteralPlan/selection/object verification. Reconstructs
// the outer forest's effect chain, sole observation, nesting and every catch.
[[nodiscard]] std::vector<std::optional<LiteralReferenceView>>
IndexLiteralReferences(
    const analysis::ValueProgram&, const EvaluationPlan&, TreeCheckBudget&,
    std::vector<std::optional<analysis::OpId>>* members = nullptr);
}  // namespace abcd::source
#endif
